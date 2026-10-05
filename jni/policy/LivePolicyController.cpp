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
    adaptive_.reset(session_id);
    thermal_hold_ = false;
    hold_since_ms_ = 0;
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
    in.runtime.active_intervention = executor_.active_key();
    in.runtime.thermal_hold = thermal_hold_;

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
        const auto in = inputs(session_id, now_ms);
        adaptive_.observe(sample(in)); // Observe impact: one fresh sample, baseline or post window

        // B-42 thermal hold: cleared only by a newer, verified Synrei state other than safety (never by
        // temperatures, an unknown or a stale state). The clearing sample executes nothing: the next
        // fresh sample is evaluated normally, and nothing is re-applied automatically.
        bool hold_cleared = false;
        if (thermal_hold_ && in.thermal && in.thermal->readable && in.thermal->verified && !in.thermal->state.empty() &&
            in.thermal->state != "safety" && in.thermal->timestamp_ms > hold_since_ms_) {
            thermal_hold_ = false;
            hold_cleared = true;
        }

        // Compare → Keep or Rollback: the active intervention is judged before anything new is decided.
        if (adaptive_.phase() == zairenkai::adaptive::Phase::Evaluating) {
            e.adaptive = adaptive_.evaluate(now_ms);
            if (e.adaptive && e.adaptive->verdict == zairenkai::adaptive::Verdict::Rollback) {
                e.execution = restore(e, in, now_ms);
                ++evaluations_;
                last_ = e;
                notify(e);
                return e;
            }
        }

        e.decision = engine_.evaluate(in);
        ExecutionContext x;
        x.session_id = session_id_;
        x.runtime = gr;
        x.thermal = sampler_.thermal_history().items().empty()
                        ? std::nullopt
                        : std::optional(sampler_.thermal_history().items().back());
        x.capabilities = capabilities_;
        const bool intervention = e.decision.action == Action::Boost || e.decision.action == Action::Mitigate;
        const std::string key = std::string(to_string(e.decision.action)) + ":" + e.decision.target;
        const std::string sig = intervention ? signature(e.decision, in) : "";
        if (intervention && hold_cleared) {
            e.adaptive_gate = "thermal_hold_cleared: fresh evidence required";
            e.execution.requested_action = e.decision.action;
            e.execution.final_status = ExecutionStatus::NotExecuted;
            e.execution.reason = "adaptive: " + e.adaptive_gate;
            ++evaluations_;
            last_ = e;
            notify(e);
            return e;
        }
        if (intervention) {
            const auto adm = adaptive_.admit(key, sig, now_ms, e.decision.action == Action::Mitigate);
            e.adaptive_gate = adm.reason;
            if (!adm.allowed) {
                e.execution.requested_action = e.decision.action;
                e.execution.final_status = ExecutionStatus::NotExecuted;
                e.execution.reason = "adaptive: " + adm.reason;
                ++evaluations_;
                last_ = e;
                notify(e);
                return e;
            }
        }
        const std::string key_before = executor_.active_key();
        e.execution = executor_.execute(e.decision, x);
        note_restore(key_before, in, e.execution);
        if (intervention) {
            using zairenkai::adaptive::ExecutionOutcome;
            const auto st = e.execution.final_status;
            const auto outcome = st == ExecutionStatus::Applied ? ExecutionOutcome::Applied
                                 : (st == ExecutionStatus::ApplyFailed || st == ExecutionStatus::VerifyFailed)
                                     ? ExecutionOutcome::Failed
                                     : ExecutionOutcome::Unchanged;
            adaptive_.executed({key, e.decision.decision_id, e.decision.target, e.decision.action == Action::Mitigate, now_ms},
                               outcome, sig, now_ms);
        } else if (e.execution.final_status == ExecutionStatus::Restored ||
                   e.execution.final_status == ExecutionStatus::RestoreFailed) {
            adaptive_.restored(e.execution.final_status == ExecutionStatus::Restored, now_ms);
        }
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
        adaptive_.restored(e.execution.final_status == ExecutionStatus::Restored, now_ms);
        last_ = e;
        notify(e);
        return e.execution.final_status == ExecutionStatus::Restored && !executor_.active();
    } catch (...) {
        return false;
    }
}

zairenkai::adaptive::Sample LivePolicyController::sample(const PolicyInputs &in) const {
    zairenkai::adaptive::Sample s;
    if (!sampler_.window().empty()) {
        const auto &w = sampler_.window().back(); // the fresh sample this tick consumes
        s.timestamp_ms = w.timestamp_ms;
        s.fps = w.fps;
        s.target_hz = w.target_hz;
        s.cpu_busy = w.cpu_busiest_core;
        s.gpu_busy = w.gpu_busy;
        s.cpu_freq_ratio = w.cpu_freq_ratio;
        s.gpu_freq_ratio = w.gpu_freq_ratio;
    }
    s.thermal = zairenkai::adaptive::from_synrei(in.thermal);
    if (in.bottleneck) {
        s.bottleneck = in.bottleneck->primary.kind;
        s.bottleneck_state = in.bottleneck->primary.rating;
        s.bottleneck_conflict = in.bottleneck->conflict;
    }
    return s;
}

std::string LivePolicyController::signature(const PolicyDecision &d, const PolicyInputs &in) const {
    // Evidence a retry must differ in: target resource, bottleneck rating, Synrei state, profile
    // and the FPS shortfall share in tenths. Deterministic, no clock.
    std::string sig = std::string(to_string(d.action)) + ":" + d.target;
    if (in.bottleneck)
        sig += std::string("|") + b::to_string(in.bottleneck->primary.kind) + "/" + b::to_string(in.bottleneck->primary.rating) +
               (in.bottleneck->conflict ? "/conflict" : "");
    sig += std::string("|thermal=") + zairenkai::adaptive::to_string(zairenkai::adaptive::from_synrei(in.thermal));
    sig += std::string("|profile=") + to_string(in.profile.mode);
    if (in.fps.fps_samples && *in.fps.fps_samples > 0 && in.fps.shortfall_samples)
        sig += "|shortfall=" + std::to_string((*in.fps.shortfall_samples * 10) / *in.fps.fps_samples);
    return sig;
}

void LivePolicyController::note_restore(const std::string &key_before, const PolicyInputs &in,
                                        const PolicyExecutionResult &r) {
    // Entering the hold: a BOOST was taken back while Synrei (verified) reports safety.
    if (r.requested_action != Action::Restore || !r.executed || key_before.rfind("BOOST:", 0) != 0) return;
    if (zairenkai::adaptive::from_synrei(in.thermal) != zairenkai::adaptive::Thermal::Safety) return;
    thermal_hold_ = true;
    hold_since_ms_ = in.thermal->timestamp_ms;
}

PolicyExecutionResult LivePolicyController::restore(LiveEvaluation &e, const PolicyInputs &in, int64_t now_ms) {
    // Rollback through the existing restoration path: a RESTORE of the executor's own transaction.
    // No value is chosen and no node is named here; PolicyExecutor finishes its transaction.
    PolicyDecision d;
    d.action = Action::Restore;
    d.target = "transaction";
    d.session_id = session_id_;
    d.evaluation_time_ms = now_ms;
    d.confidence = flux::context::Confidence::High;
    d.reason = "Adaptive evaluation: " + e.adaptive->reason + "; restoring the intervention.";
    d.constraints.push_back({ConstraintKind::RestoreRequired, "adaptive", e.adaptive->reason});
    d.decision_id = "pd-" + std::to_string(now_ms) + "-adaptive-" + (adaptive_.active() ? adaptive_.active()->decision_id : "none");
    e.decision = d;
    ExecutionContext x;
    x.session_id = session_id_;
    x.runtime = runtime_state();
    x.capabilities = capabilities_;
    const std::string key_before = executor_.active_key();
    auto r = executor_.execute(d, x);
    note_restore(key_before, in, r);
    adaptive_.restored(r.final_status == ExecutionStatus::Restored, now_ms);
    return r;
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
