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

#include "GameRuntimeHost.hpp"
#include "SessionRecorder.hpp"

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

/// Owns nothing itself: forwards to the Game Runtime performance host, which owns the transaction.
class RuntimeParticipant final : public flux::session::SessionParticipant {
public:
    const char *name() const override { return "game_runtime"; }
    void recover() override { flux_runtime::host().on_daemon_start(); }
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

} // namespace

flux::session::SessionManager &manager() {
    static flux::session::SessionManager instance = [] {
        flux::session::SessionManager m;
        m.add(&runtime_participant);
        m.add(&recorder_participant);
        return m;
    }();
    return instance;
}

} // namespace flux_session
