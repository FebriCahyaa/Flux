#include "LivePolicyController.hpp"

#include "BottleneckResult.hpp"

namespace flux::policy {

namespace b = flux::bottleneck;
namespace rt = flux::runtime;

LivePolicyController::LivePolicyController(const flux::metrics::RuntimeMetricsSampler &sampler,
                                           const flux::context::CapabilityContext *capabilities,
                                           PolicyExecutor &executor, ProfileProvider profile, RuntimeProvider runtime)
    : sampler_(sampler), capabilities_(capabilities), executor_(executor), profile_(std::move(profile)),
      runtime_(std::move(runtime)) {}

void LivePolicyController::begin(const std::string &session_id) {
    session_id_ = session_id;
    enabled_ = true;
    seen_samples_ = sampler_.samples_taken(); // only samples taken from now on are fresh
    skipped_.clear();
}

RuntimeEvidence LivePolicyController::runtime_state() const {
    RuntimeEvidence r;
    if (runtime_) r = runtime_();
    r.recovery_failed = r.recovery_failed || recovery_failed_;
    return r;
}

PolicyInputs LivePolicyController::inputs(const std::string &session_id, int64_t now_ms) const {
    PolicyInputs in;
    in.session_id = session_id;
    in.evaluation_time_ms = now_ms;
    in.capabilities = capabilities_;
    if (profile_) in.profile = profile_();
    const auto gr = runtime_state();
    in.runtime.game_active = gr.game_active;
    in.runtime.recovery_failed = gr.recovery_failed;
    // The decision concerns the executor's own changes; GameRuntime's transaction is validated
    // separately by the executor (ExecutionContext).
    in.runtime.transaction = executor_.active() ? rt::TxState::Active : rt::TxState::Inactive;

    // Thermal: the newest Synrei snapshot the sampler recorded, as recorded (stale stays stale).
    const auto &th = sampler_.thermal_history().items();
    if (!th.empty()) in.thermal = th.back();

    // FPS: the sampler window (SessionRecorder observations), Hz and FPS kept apart.
    FpsEvidence fps;
    int with_fps = 0, below = 0;
    for (const auto &s : sampler_.window()) {
        if (!s.fps) continue;
        ++with_fps;
        fps.observed_fps = s.fps;
        if (s.target_hz) {
            fps.target_refresh_hz = s.target_hz;
            if (*s.fps < *s.target_hz * b::kDeficit) ++below;
        }
    }
    if (with_fps > 0) {
        fps.fps_samples = with_fps;
        fps.shortfall_samples = below;
        fps.source = "fps_observation";
    }
    in.fps = fps;

    // Bottleneck: in-session judgement of the live window (const; the final result is untouched).
    b::PerformanceState perf;
    perf.profile = to_string(in.profile.mode);
    in.bottleneck = b::make_result(sampler_.assess(now_ms, perf), session_id);
    return in;
}

void LivePolicyController::notify(const LiveEvaluation &e) const {
    if (!observer_) return;
    try {
        observer_(e);
    } catch (...) {
    }
}

std::optional<LiveEvaluation> LivePolicyController::tick(const std::string &session_id, int64_t now_ms) {
    try {
        skipped_.clear();
        if (!enabled_) return skipped_ = "disabled", std::nullopt;
        if (session_id.empty() || session_id != session_id_) return skipped_ = "session_mismatch", std::nullopt;
        if (!sampler_.running() || sampler_.session_id() != session_id)
            return skipped_ = "no_live_metrics", std::nullopt;
        const uint64_t taken = sampler_.samples_taken();
        if (taken <= seen_samples_) return skipped_ = "no_fresh_evidence", std::nullopt;
        seen_samples_ = taken; // consumed, whatever the outcome
        if (!capabilities_) return skipped_ = "no_capabilities", std::nullopt;
        const auto gr = runtime_state();
        if (gr.recovery_failed) return skipped_ = "recovery_incomplete", std::nullopt;
        if (!gr.game_active) return skipped_ = "no_active_game", std::nullopt;
        if (gr.transaction == rt::TxState::Preparing || gr.transaction == rt::TxState::Restoring ||
            gr.transaction == rt::TxState::Failed)
            return skipped_ = std::string("conflicting_transaction: ") + rt::to_string(gr.transaction), std::nullopt;

        LiveEvaluation e;
        e.session_id = session_id;
        e.sample_index = taken;
        e.decision = engine_.evaluate(inputs(session_id, now_ms));
        ExecutionContext x;
        x.session_id = session_id_;
        x.runtime = gr;
        x.thermal = sampler_.thermal_history().items().empty()
                        ? std::nullopt
                        : std::optional(sampler_.thermal_history().items().back());
        x.capabilities = capabilities_;
        e.execution = executor_.execute(e.decision, x);
        ++evaluations_;
        last_ = e;
        notify(e);
        return e;
    } catch (const std::exception &ex) {
        skipped_ = std::string("evaluation_failed: ") + ex.what();
    } catch (...) {
        skipped_ = "evaluation_failed";
    }
    return std::nullopt;
}

bool LivePolicyController::end(const std::string &session_id, int64_t now_ms) {
    enabled_ = false; // 1. stop evaluation
    if (!executor_.active()) return true;
    try {
        // 2. RESTORE through the DecisionEngine (game over, executor transaction active).
        PolicyInputs in;
        in.session_id = session_id;
        in.evaluation_time_ms = now_ms;
        in.capabilities = capabilities_;
        in.runtime.game_active = false;
        in.runtime.transaction = rt::TxState::Active;
        LiveEvaluation e;
        e.session_id = session_id;
        e.sample_index = sampler_.samples_taken();
        e.decision = engine_.evaluate(in);
        ExecutionContext x;
        x.session_id = session_id;
        x.runtime = in.runtime;
        x.capabilities = capabilities_;
        e.execution = executor_.execute(e.decision, x);
        last_ = e;
        notify(e);
        return e.execution.final_status == ExecutionStatus::Restored && !executor_.active();
    } catch (...) {
        return false;
    }
}

void LivePolicyParticipant::recover() {
    try {
        controller_.set_recovery_failed(recover_ ? !recover_() : false);
    } catch (...) {
        controller_.set_recovery_failed(true);
    }
}

void LivePolicyParticipant::begin(const flux::session::SessionInfo &s) {
    try {
        controller_.begin(s.id);
    } catch (...) {
    }
}

bool LivePolicyParticipant::end(const flux::session::SessionInfo &s, flux::session::EndReason) {
    try {
        return controller_.end(s.id, s.ended_ms ? s.ended_ms : s.started_ms);
    } catch (...) {
        return false;
    }
}

void LivePolicyParticipant::tick(const flux::session::SessionInfo &s, int64_t now_ms) {
    controller_.tick(s.id, now_ms);
}

} // namespace flux::policy
