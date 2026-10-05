// The output of the Decision Engine (Step 8.13). A decision is a recommendation with evidence;
// it is never executed here. Decision != Execution.
#pragma once

#include "PolicyConstraint.hpp"
#include "PolicyEvidence.hpp"

#include <string>
#include <vector>

namespace flux::policy {

/// Exactly five initial actions. Priority: RESTORE > NO_ACTION > MITIGATE > BOOST
/// (OBSERVE is the evidence-gathering fallback whenever a stronger action is not justified).
enum class Action { NoAction, Observe, Mitigate, Boost, Restore };
const char *to_string(Action a);

struct PolicyDecision {
    std::string decision_id;  // deterministic: derived from the inputs that produced it
    Action action = Action::Observe;
    flux::context::Confidence confidence = flux::context::Confidence::None;
    std::string target;       // resource the action concerns ("cpu", "gpu", ...); "" when none
    std::string reason;
    std::vector<EvidenceRef> supporting_evidence;
    std::vector<EvidenceRef> blocking_evidence;
    std::vector<PolicyConstraint> constraints;
    std::vector<std::string> limitations;
    std::string session_id;
    int64_t evaluation_time_ms = 0;
};

/// Human-readable explanation: what, why, supporting / blocking evidence, constraints, limitations.
std::string explain(const PolicyDecision &d);

} // namespace flux::policy
