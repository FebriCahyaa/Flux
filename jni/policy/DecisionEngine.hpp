// Evidence-driven Decision Engine (Step 8.13) — reasoning only.
//
// Observe -> Understand -> Decide -> (Execute -> Verify -> Observe). This step ends at Decide:
// evaluate() returns a PolicyDecision and touches nothing — no sysfs/procfs/kernel/thermal/
// refresh/GPU/CPU/memory/storage writes, no GameRuntime, PerformancePlanner, Synrei,
// SessionRecorder or Observatory changes. There is no executor. Deterministic: the same inputs
// give the same decision (no clock, no randomness).
#pragma once

#include "PolicyDecision.hpp"

namespace flux::policy {

class DecisionEngine {
  public:
    /// Never throws: an internal failure yields OBSERVE with the failure as a limitation.
    PolicyDecision evaluate(const PolicyInputs &in) const;
};

} // namespace flux::policy
