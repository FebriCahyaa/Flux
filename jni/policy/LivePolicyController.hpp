// Live policy integration (Phase 4C).
//
// Observe → Understand → Decide → Validate → Execute → Verify → Restore if required → Observe.
//
// Connects the existing pieces during a session and adds no new mechanism:
//   evidence   RuntimeMetricsSampler (window, Synrei thermal history, in-session assess())
//   decision   DecisionEngine::evaluate (unchanged, deterministic)
//   execution  PolicyExecutor::execute (trusted operations, Transaction Engine only)
// No collector, no thread, no persistence, no transaction framework of its own. Evaluation runs
// on the session tick only when the sampler produced a new sample (fresh evidence); the same
// evidence is never evaluated twice and an already applied action is not re-executed
// (PolicyExecutor reports AlreadyActive). The session-final BOTTLENECK_ASSESSED result is not
// used and not touched: assess() is const.
//
// Shutdown: the participant is registered last, so SessionManager ends it FIRST — the executor's
// transaction is restored before the sampler stops and before GameRuntime restores its own.
#pragma once

#include "AdaptiveController.hpp"
#include "DecisionEngine.hpp"
#include "PolicyExecutor.hpp"
#include "RuntimeMetricsSampler.hpp"
#include "SessionManager.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace flux::policy {

/// Live inputs that are not evidence collectors: current profile intent and the GameRuntime /
/// recovery state (from the existing GameRuntime host). Both optional.
using ProfileProvider = std::function<ProfileIntent()>;
using RuntimeProvider = std::function<RuntimeEvidence()>;

struct LiveEvaluation {
    std::string session_id;
    uint64_t sample_index = 0; // sampler sample count the evaluation used
    PolicyDecision decision;
    PolicyExecutionResult execution;
    std::string adaptive_gate;                                  // admission result for BOOST/MITIGATE ("" otherwise)
    std::optional<zairenkai::adaptive::Evaluation> adaptive;     // Keep/Rollback/Observe of the active intervention
};
using LiveObserver = std::function<void(const LiveEvaluation &)>;

class LivePolicyController {
  public:
    LivePolicyController(const flux::metrics::RuntimeMetricsSampler &sampler,
                         const flux::context::CapabilityContext *capabilities, PolicyExecutor &executor,
                         ProfileProvider profile = nullptr, RuntimeProvider runtime = nullptr);

    /// Enables evaluation for this session. Evidence already sampled is not "fresh".
    void begin(const std::string &session_id);
    /// Evaluates once per new sampler sample; nullopt when nothing was evaluated. Never throws.
    std::optional<LiveEvaluation> tick(const std::string &session_id, int64_t now_ms);
    /// Stops evaluation, then restores the executor's transaction (DecisionEngine RESTORE).
    /// True when nothing was left applied. Never throws.
    bool end(const std::string &session_id, int64_t now_ms);

    /// Daemon recovery is incomplete (journal kept): evaluation stays blocked.
    void set_recovery_failed(bool failed) { recovery_failed_ = failed; }
    void set_observer(LiveObserver o) { observer_ = std::move(o); }

    bool enabled() const { return enabled_; }
    uint64_t evaluations() const { return evaluations_; }
    const std::optional<LiveEvaluation> &last() const { return last_; }
    /// Why the most recent tick did not evaluate ("" when it did).
    const std::string &skipped() const { return skipped_; }
    /// Adaptive Optimization V1 (Phase 5): evaluation of the executed intervention's outcome.
    const zairenkai::adaptive::AdaptiveController &adaptive() const { return adaptive_; }

    /// Inputs exactly as tick() would build them (exposed for tests / explanation).
    PolicyInputs inputs(const std::string &session_id, int64_t now_ms) const;

  private:
    RuntimeEvidence runtime_state() const;
    void notify(const LiveEvaluation &e) const;
    zairenkai::adaptive::Sample sample(const PolicyInputs &in) const;
    std::string signature(const PolicyDecision &d, const PolicyInputs &in) const;
    PolicyExecutionResult restore(LiveEvaluation &e, int64_t now_ms);

    const flux::metrics::RuntimeMetricsSampler &sampler_;
    const flux::context::CapabilityContext *capabilities_;
    PolicyExecutor &executor_;
    ProfileProvider profile_;
    RuntimeProvider runtime_;
    LiveObserver observer_;
    DecisionEngine engine_;
    zairenkai::adaptive::AdaptiveController adaptive_;
    bool enabled_ = false;
    bool recovery_failed_ = false;
    std::string session_id_;
    uint64_t seen_samples_ = 0;
    uint64_t evaluations_ = 0;
    std::optional<LiveEvaluation> last_;
    std::string skipped_;
};

/// Session lifecycle adapter. Register it LAST: it begins after RuntimeMetrics and ends first.
class LivePolicyParticipant final : public flux::session::SessionParticipant {
  public:
    using Recover = std::function<bool()>; // replays the executor's journal; true when clean
    LivePolicyParticipant(LivePolicyController &controller, Recover recover = nullptr)
        : controller_(controller), recover_(std::move(recover)) {}
    const char *name() const override { return "live_policy"; }
    void recover() override;
    void begin(const flux::session::SessionInfo &s) override;
    bool end(const flux::session::SessionInfo &s, flux::session::EndReason why) override;
    void tick(const flux::session::SessionInfo &s, int64_t now_ms) override;
    bool needs_tick() const override { return controller_.enabled(); }

  private:
    LivePolicyController &controller_;
    Recover recover_;
};

} // namespace flux::policy
