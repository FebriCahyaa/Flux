#include "AdaptiveController.hpp"

namespace zairenkai::adaptive {

const char *to_string(Phase p) {
    switch (p) {
    case Phase::Idle: return "idle";
    case Phase::Evaluating: return "evaluating";
    case Phase::Kept: return "kept";
    case Phase::Cooldown: return "cooldown";
    case Phase::Held: return "held";
    }
    return "idle";
}

void AdaptiveController::reset(const std::string &session_id) { *this = AdaptiveController(), session_ = session_id; }

void AdaptiveController::observe(const Sample &s) {
    if (phase_ == Phase::Evaluating) {
        post_.push_back(s);
        return;
    }
    if (active_) return; // a kept intervention changes the system: not a baseline for the next one
    baseline_.push_back(s);
    while (baseline_.size() > kBaselineWindow) baseline_.pop_front();
}

void AdaptiveController::cooldown(int64_t now_ms, int64_t span) {
    cooldown_until_ = now_ms + span;
    if (phase_ != Phase::Held) phase_ = active_ ? Phase::Kept : Phase::Cooldown;
}

Admission AdaptiveController::admit(const std::string &key, const std::string &signature, int64_t now_ms,
                                     bool mitigation) const {
    auto no = [](std::string why) { return Admission{false, std::move(why)}; };
    if (restore_failed_) return no("restore_failed: no further interventions this session");
    if (phase_ == Phase::Held) return no("held: rollback limit reached this session");
    if (phase_ == Phase::Evaluating) return no("evaluating: previous intervention not yet judged");
    if (active_) return no("active: an intervention is in place; no automatic escalation");
    if (now_ms < cooldown_until_) return no("cooldown: until " + std::to_string(cooldown_until_));
    if (auto it = rollbacks_.find(key); it != rollbacks_.end() && it->second >= kMaxRollbacksPerKey)
        return no("hysteresis: " + key + " rolled back " + std::to_string(it->second) + " times this session");
    if (auto it = rolled_back_.find(key); it != rolled_back_.end() && it->second == signature)
        return no("hysteresis: " + key + " was rolled back under identical evidence");
    if (mitigation) return {true, "admitted"};
    size_t fps = 0;
    for (const auto &s : baseline_)
        if (s.fps && s.target_hz && *s.target_hz > 0) ++fps;
    if (fps < kMinBaselineFps) return no("insufficient_baseline: " + std::to_string(fps) + " FPS samples");
    return {true, "admitted"};
}

void AdaptiveController::executed(const Intervention &i, ExecutionOutcome outcome, const std::string &signature,
                                  int64_t now_ms) {
    switch (outcome) {
    case ExecutionOutcome::Unchanged: return;
    case ExecutionOutcome::Failed:
        cooldown(now_ms, kCooldownAfterFailureMs);
        rolled_back_[i.key] = signature; // same evidence will not retry a failing write
        return;
    case ExecutionOutcome::Applied: break;
    }
    active_ = i;
    active_->applied_ms = now_ms;
    frozen_baseline_.assign(baseline_.begin(), baseline_.end());
    post_.clear();
    phase_ = Phase::Evaluating;
    pending_signature_ = signature;
}

std::optional<Evaluation> AdaptiveController::evaluate(int64_t now_ms) {
    if (phase_ != Phase::Evaluating || !active_) return std::nullopt;
    auto e = adaptive::evaluate(frozen_baseline_, post_, active_->target, active_->mitigation);
    last_ = e;
    if (e.verdict == Verdict::Keep) {
        phase_ = Phase::Kept;
        cooldown(now_ms, kCooldownAfterKeepMs);
    } else if (e.verdict == Verdict::Rollback) {
        rollback_pending_ = true; // the caller restores through PolicyExecutor, then calls restored()
    }
    return e;
}

void AdaptiveController::restored(bool clean, int64_t now_ms) {
    if (!active_) return;
    const auto key = active_->key;
    if (!clean) restore_failed_ = true;
    if (rollback_pending_) {
        rolled_back_[key] = pending_signature_;
        if (++rollbacks_[key] >= kMaxRollbacksPerKey) phase_ = Phase::Held;
    }
    rollback_pending_ = false;
    active_.reset();
    post_.clear();
    baseline_.clear(); // the system changed twice: the next baseline starts fresh
    if (phase_ != Phase::Held) phase_ = Phase::Cooldown;
    cooldown(now_ms, kCooldownAfterRollbackMs);
}

} // namespace zairenkai::adaptive
