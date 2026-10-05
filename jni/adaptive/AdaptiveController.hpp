// Adaptive Optimization V1 (Phase 5) — evaluation of executed interventions.
//
// Observe → Understand → Decide → Verify capability → Execute → Verify execution → Observe impact
// → Compare → Keep or Rollback → Cooldown → Continue observation.
//
// The controller owns only the evaluation of an intervention's outcome. It never decides an
// action (DecisionEngine), never executes or writes (PolicyExecutor + Transaction Engine), never
// chooses a value, and never escalates: a stronger step always needs a new DecisionEngine
// evaluation on fresh evidence. It is fed by the live policy loop on the existing session tick
// (no thread, no collector, no persistence). Time is the evidence timestamp supplied by the caller.
#pragma once

#include "InterventionEvaluation.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace zairenkai::adaptive {

// Bounded cooldowns and hysteresis (documented in ADAPTIVE_OPTIMIZATION.md).
inline constexpr int64_t kCooldownAfterKeepMs = 30000;
inline constexpr int64_t kCooldownAfterRollbackMs = 60000;
inline constexpr int64_t kCooldownAfterFailureMs = 60000;
inline constexpr size_t kBaselineWindow = 10;   // newest samples kept as the baseline
inline constexpr int kMaxRollbacksPerKey = 2;   // then that action/target is held for the session

/// What was executed, in the live loop's own terms (no new decision vocabulary).
struct Intervention {
    std::string key;          // "<ACTION>:<target>", e.g. "BOOST:cpu"
    std::string decision_id;
    std::string target;       // "cpu" / "gpu"
    bool mitigation = false;  // MITIGATE (thermal relief) vs BOOST (performance)
    int64_t applied_ms = 0;
};

enum class ExecutionOutcome { Applied, Failed, Unchanged };

enum class Phase { Idle, Evaluating, Kept, Cooldown, Held };
const char *to_string(Phase p);

struct Admission {
    bool allowed = false;
    std::string reason; // "admitted" or why not (cooldown, hysteresis, evaluating, ...)
};

class AdaptiveController {
  public:
    void reset(const std::string &session_id);

    /// One fresh sample (once per sampler sample). Feeds the baseline, or the post window while evaluating.
    void observe(const Sample &s);

    /// Gate before a BOOST/MITIGATE is executed. `signature` describes the evidence the decision rests on.
    /// mitigation: a safety-driven MITIGATE needs no performance baseline (safety takes precedence).
    Admission admit(const std::string &key, const std::string &signature, int64_t now_ms, bool mitigation = false) const;

    /// After PolicyExecutor ran an admitted decision.
    void executed(const Intervention &i, ExecutionOutcome outcome, const std::string &signature, int64_t now_ms);

    /// While evaluating: Observe until the evidence is sufficient, then Keep or Rollback.
    std::optional<Evaluation> evaluate(int64_t now_ms);

    /// The intervention was restored (rollback requested here, or a DecisionEngine RESTORE).
    void restored(bool clean, int64_t now_ms);

    Phase phase() const { return phase_; }
    const std::optional<Intervention> &active() const { return active_; }
    const std::deque<Sample> &baseline() const { return baseline_; }
    const std::vector<Sample> &post() const { return post_; }
    int64_t cooldown_until() const { return cooldown_until_; }
    const std::optional<Evaluation> &last_evaluation() const { return last_; }
    bool restore_failed() const { return restore_failed_; }

  private:
    void cooldown(int64_t now_ms, int64_t span);
    std::string session_;
    Phase phase_ = Phase::Idle;
    std::deque<Sample> baseline_;
    std::vector<Sample> frozen_baseline_; // baseline at the moment of execution
    std::vector<Sample> post_;
    std::optional<Intervention> active_;
    std::optional<Evaluation> last_;
    int64_t cooldown_until_ = 0;
    std::string pending_signature_; // evidence the active intervention was admitted under
    bool rollback_pending_ = false;
    bool restore_failed_ = false;
    std::map<std::string, std::string> rolled_back_; // key -> evidence signature at rollback
    std::map<std::string, int> rollbacks_;
};

} // namespace zairenkai::adaptive
