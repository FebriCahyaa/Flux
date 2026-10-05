/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SessionHost.hpp"

#include "BottleneckEvents.hpp"
#include "CapabilityHost.hpp"
#include "GameRuntimeHost.hpp"
#include "LivePolicyController.hpp"
#include "ObservatoryHost.hpp"
#include "RuntimeMetricsSampler.hpp"
#include "SessionRecorder.hpp"
#include "SynreiThermalAdapter.hpp"

#include <chrono>
#include <ctime>

#include <FluxLog.hpp>

namespace flux_session {

namespace {

using flux::session::EndReason;
using flux::session::SessionInfo;

flux::perf::EndReason to_runtime(EndReason r) {
    switch (r) {
    case EndReason::ProcessDeath: return flux::perf::EndReason::ProcessDeath;
    case EndReason::Switch: return flux::perf::EndReason::Switch;
    case EndReason::Failure: return flux::perf::EndReason::Failure;
    case EndReason::DaemonStop: return flux::perf::EndReason::DaemonStop;
    case EndReason::Exit:
    case EndReason::FocusLost: return flux::perf::EndReason::Exit;
    }
    return flux::perf::EndReason::Exit;
}

/// GameRuntime journal replay at daemon start left something unrestored (live policy stays off).
bool runtime_recovery_failed = false;

/// Owns nothing itself: forwards to the Game Runtime performance host, which owns the transaction.
class RuntimeParticipant final : public flux::session::SessionParticipant {
public:
    const char *name() const override { return "game_runtime"; }
    void recover() override {
        runtime_recovery_failed = false;
        for (const auto &r : flux_runtime::host().on_daemon_start())
            if (!r.report.clean()) runtime_recovery_failed = true;
    }
    void begin(const SessionInfo &s) override {
        flux_runtime::host().on_game_active(s.key.package, s.key.pid, s.started_ms);
    }
    bool end(const SessionInfo &, EndReason why) override {
        return flux_runtime::host().on_game_end(to_runtime(why));
    }
    void profile_applied(const SessionInfo &) override { flux_runtime::host().on_profile_applied(); }
    void tick(const SessionInfo &, int64_t now_ms) override { flux_runtime::host().tick(now_ms); }
    bool needs_tick() const override { return flux_runtime::host().needs_tick(); }
};

/// Existing statistics recorder; its files (session_live.json, sessions.json) are unchanged.
class RecorderParticipant final : public flux::session::SessionParticipant {
public:
    const char *name() const override { return "session_recorder"; }
    void begin(const SessionInfo &s) override {
        std::vector<pid_t> pids(s.key.pids.begin(), s.key.pids.end());
        SessionRecorder::get_instance().start(s.key.package, pids);
    }
    bool end(const SessionInfo &, EndReason) override {
        SessionRecorder::get_instance().stop();
        return true;
    }
};

RuntimeParticipant runtime_participant;
RecorderParticipant recorder_participant;

/// Read-only runtime metrics for the bottleneck model (Step 8.8). Samples on the session tick
/// only (no thread), every kSamplingIntervalMs; stops with the session. FPS is read from
/// SessionRecorder's published observation (Step 8.8.1); it is never measured here.
constexpr int64_t kSamplingIntervalMs = 2000;
flux::metrics::RuntimeMetricsSampler &sampler() {
    static auto fs = flux::kernel::make_readonly_fs("");
    // Synrei (HiCo) thermal context: read-only view of /dev/hico/state (Step 8.9).
    static flux::thermal::SynreiThermalAdapter synrei(*fs, [] { return static_cast<int64_t>(std::time(nullptr)); });
    static flux::metrics::RuntimeMetricsSampler instance(
        *fs, flux_capability::context().get(), {kSamplingIntervalMs, 120, 3000},
        [] { return SessionRecorder::get_instance().fps_observation().latest(); },
        [](int64_t now_ms) { return synrei.read(now_ms); });
    // Final bottleneck result at session end -> Observatory (in-memory store; Step 8.10).
    static const bool sink_set = [] {
        const auto wall = [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        };
        instance.set_result_sink(
            [wall](const flux::bottleneck::BottleneckResult &r) {
                flux_observatory::sink().write(flux::bridge::bottleneck_event(r, wall()));
            },
            [wall](const std::string &session, const std::string &error, int64_t) {
                flux_observatory::sink().write(flux::bridge::bottleneck_failure_event(session, error, wall()));
            });
        return true;
    }();
    (void)sink_set;
    return instance;
}
flux::metrics::SamplerParticipant &sampler_participant() {
    static flux::metrics::SamplerParticipant p(sampler());
    return p;
}

/// Live policy (Phase 4C): DecisionEngine + PolicyExecutor on the session tick, fed by the sampler
/// above (no second collector, no thread). Its own journal lets a restarted daemon restore what a
/// crashed one applied.
constexpr const char *kPolicyJournal = "/data/adb/.config/zairenkai/policy.journal";
std::function<FluxProfileMode()> profile_source;

flux::policy::ProfileMode to_policy(FluxProfileMode m) {
    switch (m) {
    case PERFCOMMON: return flux::policy::ProfileMode::PerfCommon;
    case PERFORMANCE_PROFILE: return flux::policy::ProfileMode::Performance;
    case PERFORMANCE_LITE_PROFILE: return flux::policy::ProfileMode::PerformanceLite;
    case BALANCE_PROFILE: return flux::policy::ProfileMode::Balance;
    case POWERSAVE_PROFILE: return flux::policy::ProfileMode::Powersave;
    }
    return flux::policy::ProfileMode::Unknown;
}

flux::perf::FileStore &policy_files() {
    static auto files = flux::perf::make_file_store();
    return files;
}

flux::policy::PolicyExecutor &policy_executor() {
    static flux::policy::PolicyExecutor instance(
        flux::perf::make_node_io(),
        [](const std::string &text) {
            if (flux::runtime::journal::parse(text).entries.empty()) return policy_files().remove(kPolicyJournal);
            return policy_files().write_atomic(kPolicyJournal, text);
        },
        flux_observatory::bridge().transaction_observer());
    return instance;
}

flux::policy::LivePolicyController &live_policy() {
    static flux::policy::LivePolicyController instance(
        sampler(), flux_capability::context().get(), policy_executor(),
        [] {
            flux::policy::ProfileIntent p;
            if (profile_source) p.mode = to_policy(profile_source());
            return p;
        },
        [] {
            flux::policy::RuntimeEvidence r;
            r.game_active = manager().active();
            switch (flux_runtime::host().runtime().state()) {
            case flux::perf::RuntimeState::Active: r.transaction = flux::runtime::TxState::Active; break;
            case flux::perf::RuntimeState::Failed: r.transaction = flux::runtime::TxState::Failed; break;
            default: r.transaction = flux::runtime::TxState::Inactive; break;
            }
            r.recovery_failed = runtime_recovery_failed;
            return r;
        });
    static const bool observed = [] {
        instance.set_observer([](const flux::policy::LiveEvaluation &e) {
            if (e.decision.action == flux::policy::Action::NoAction || e.decision.action == flux::policy::Action::Observe)
                return;
            LOGI_TAG("Policy", "{} {} -> {}: {}", flux::policy::to_string(e.decision.action), e.decision.target,
                     flux::policy::to_string(e.execution.final_status), e.execution.reason);
        });
        return true;
    }();
    (void)observed;
    return instance;
}

flux::policy::LivePolicyParticipant &live_policy_participant() {
    static flux::policy::LivePolicyParticipant p(live_policy(), [] {
        // A crash while a policy change was applied: put the original values back first.
        const auto leftover = policy_files().read(kPolicyJournal);
        if (!leftover) return true;
        const auto report = flux::runtime::recover(flux::perf::make_node_io(), *leftover);
        if (!report.clean()) {
            LOGW_TAG("Policy", "policy journal not fully restored ({} failed); live policy disabled",
                     report.failed.size());
            return false;
        }
        policy_files().remove(kPolicyJournal);
        LOGI_TAG("Policy", "policy journal replayed: {} value(s) restored", report.restored);
        return true;
    });
    return p;
}

} // namespace

void set_profile_source(std::function<FluxProfileMode()> source) { profile_source = std::move(source); }

flux::session::SessionManager &manager() {
    static flux::session::SessionManager instance = [] {
        flux::session::SessionManager m;
        m.add(&runtime_participant);
        m.add(&recorder_participant);
        // Last to begin, first to end: sampling stops before any restore runs.
        m.add(&sampler_participant());
        // Live policy LAST: begins after metrics, ends FIRST, so the PolicyExecutor transaction is
        // restored before sampling stops and before GameRuntime restores its own values.
        m.add(&live_policy_participant());
        m.set_observer(flux_observatory::bridge().session_observer());
        m.set_context(flux_observatory::bridge().session_context());
        return m;
    }();
    return instance;
}

} // namespace flux_session
