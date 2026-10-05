// Constraints that limit or block a decision (Step 8.13), plus capability gating.
#pragma once

#include "PolicyEvidence.hpp"

#include <string>
#include <vector>

namespace flux::policy {

enum class ConstraintKind {
    RestoreRequired,      // runtime/transaction state requires restoration first
    ThermalSafety,        // Synrei reports `safety`: performance intent is overridden
    ThermalUnknown,       // thermal context missing, stale, unverified, or not reporting a constraint
    CapabilityBlocked,    // no supported + readable + writable control capability
    CapabilityRestricted, // writable but unverified (or high risk / no rollback): not actionable yet
    Conflict,             // the bottleneck result is in conflict
    InsufficientEvidence, // missing / weak bottleneck or FPS evidence
    ProfileIntent,        // the profile does not request the stronger action
    History,              // repeated restore failures in the history window
};
const char *to_string(ConstraintKind k);

struct PolicyConstraint {
    ConstraintKind kind;
    std::string subject; // e.g. "boost", "gpu", "cpufreq.policy4.scaling_max_freq"
    std::string detail;
};

/// Result of gating a resource ("cpu", "gpu", "memory", "storage") against the capability context.
struct CapabilityGate {
    enum class Verdict { Blocked, Restricted, Actionable } verdict = Verdict::Blocked;
    std::vector<EvidenceRef> actionable;  // verified + writable + readable + supported + rollback + risk below high
    std::vector<EvidenceRef> blocking;    // why each candidate was not actionable (bounded)
    std::string summary;
};
const char *to_string(CapabilityGate::Verdict v);

/// Gates only controls that a later executor could undo (rollback-capable capability facts).
/// unsupported / unreadable / non-writable -> blocked; writable but unverified -> restricted.
CapabilityGate gate(const flux::context::CapabilityContext *capabilities, const std::string &resource);

} // namespace flux::policy
