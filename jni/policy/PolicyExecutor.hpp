// Controlled Policy Executor (Phase 4B).
//
// Observe → Understand → Decide → Validate → Execute → Verify → Restore if required → Observe.
// The executor does not decide: it executes an already approved PolicyDecision exactly as given
// (never OBSERVE→MITIGATE, never MITIGATE→BOOST), only through trusted operation definitions, and
// only through the existing Transaction Engine (snapshot, journal, apply, read-back verify,
// rollback, restore). It discovers no nodes, accepts no paths or commands, chooses no
// optimisation values beyond the operation's fixed, capability-listed step, and has no loop.
#pragma once

#include "PolicyDecision.hpp"
#include "Transaction.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flux::policy {

/// State the executor validates against (from existing GameRuntime / Transaction / Synrei contracts).
struct ExecutionContext {
    std::string session_id;                                  // the currently active session
    RuntimeEvidence runtime;                                 // game active, GameRuntime TxState, recovery
    std::optional<flux::thermal::ThermalSnapshot> thermal;   // only a verified `safety` blocks BOOST
    const flux::context::CapabilityContext *capabilities = nullptr;
};

enum class ExecutionStatus {
    NotExecuted,      // NO_ACTION / OBSERVE
    Blocked,          // refused before the Transaction Engine
    NoChange,         // every permitted control already at the operation's limit
    AlreadyActive,    // the same action/target is already applied by this executor
    Applied,          // transaction started: written and read back
    ApplyFailed,      // a write was rejected; rolled back by the engine
    VerifyFailed,     // read-back differed; rolled back by the engine
    Restored,         // RESTORE: the executor's transaction restored and read back
    RestoreFailed,    // RESTORE: not fully restored; journal kept
    NothingToRestore, // RESTORE without an executor-owned transaction
};
const char *to_string(ExecutionStatus s);

struct AffectedCapability {
    std::string capability_id, operation, path;
    std::string before, target, after;
};

struct PolicyExecutionResult {
    std::string execution_id;
    Action requested_action = Action::Observe;
    ExecutionStatus final_status = ExecutionStatus::NotExecuted;
    bool executed = false;    // operations reached the Transaction Engine
    bool verified = false;    // transaction read-back succeeded for every operation
    bool rolled_back = false; // the engine rolled back after a failure
    bool restored = false;    // read-back confirmed the original values
    std::string reason;
    std::vector<std::string> blocked_constraints; // exact reasons, e.g. "cpufreq.policy4.scaling_max_freq: capability_unverified"
    std::string transaction_id;
    std::vector<AffectedCapability> affected_capabilities;
    std::vector<std::string> limitations;
};
std::string to_text(const PolicyExecutionResult &r);

class PolicyExecutor {
  public:
    /// io: the daemon's node I/O, used only by the Transaction Engine's NodeWriteOperation (writes)
    /// and for reading current values / frequency lists. journal + observer go to the Transaction.
    PolicyExecutor(flux::runtime::Io io, flux::runtime::Transaction::JournalSink journal = nullptr,
                   flux::runtime::TxObserver observer = nullptr);
    ~PolicyExecutor();

    PolicyExecutionResult execute(const PolicyDecision &decision, const ExecutionContext &context);

    bool active() const { return active_ != nullptr; }
    const std::string &active_key() const { return active_key_; }

  private:
    flux::runtime::Io io_;
    flux::runtime::Transaction::JournalSink journal_;
    flux::runtime::TxObserver observer_;
    std::unique_ptr<flux::runtime::Transaction> active_; // at most one executor-owned transaction
    std::string active_key_;                              // "<ACTION>:<target>"
    uint64_t seq_ = 0;
};

} // namespace flux::policy
