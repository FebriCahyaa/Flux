#include "PolicyConstraint.hpp"

#include <map>

namespace flux::policy {

namespace cx = flux::context;

const char *to_string(ProfileMode m) {
    switch (m) {
    case ProfileMode::PerfCommon: return "perfcommon";
    case ProfileMode::Performance: return "performance";
    case ProfileMode::PerformanceLite: return "performance_lite";
    case ProfileMode::Balance: return "balance";
    case ProfileMode::Powersave: return "powersave";
    case ProfileMode::Unknown: break;
    }
    return "unknown";
}

const char *to_string(ConstraintKind k) {
    switch (k) {
    case ConstraintKind::RestoreRequired: return "restore_required";
    case ConstraintKind::ThermalSafety: return "thermal_safety";
    case ConstraintKind::ThermalUnknown: return "thermal_unknown";
    case ConstraintKind::CapabilityBlocked: return "capability_blocked";
    case ConstraintKind::CapabilityRestricted: return "capability_restricted";
    case ConstraintKind::Conflict: return "conflict";
    case ConstraintKind::InsufficientEvidence: return "insufficient_evidence";
    case ConstraintKind::ProfileIntent: return "profile_intent";
    case ConstraintKind::History: return "history";
    }
    return "unknown";
}

const char *to_string(CapabilityGate::Verdict v) {
    switch (v) {
    case CapabilityGate::Verdict::Actionable: return "actionable";
    case CapabilityGate::Verdict::Restricted: return "restricted";
    case CapabilityGate::Verdict::Blocked: break;
    }
    return "blocked";
}

CapabilityGate gate(const cx::CapabilityContext *capabilities, const std::string &resource) {
    static const std::map<std::string, std::vector<std::string>> kControls = {
        {"cpu", {"cpufreq", "governor"}}, {"gpu", {"gpu"}}, {"memory", {"swap", "zram"}}, {"storage", {"io_scheduler"}}};
    CapabilityGate g;
    auto block = [&](const std::string &ref, const std::string &why) {
        if (g.blocking.size() < 8) g.blocking.push_back({"capability", ref, why});
    };
    const auto domains = kControls.find(resource);
    if (domains == kControls.end()) {
        g.summary = "no control capability is defined for " + resource;
        block(resource, g.summary);
        return g;
    }
    if (!capabilities) {
        g.summary = "no capability context available";
        block(resource, g.summary);
        return g;
    }
    bool restricted = false;
    for (const auto &domain : domains->second) {
        for (const auto &id : capabilities->ids(domain)) {
            const auto r = capabilities->resolve(id);
            const auto all = capabilities->facts(id);
            const cx::CapabilityFact *f = r.fact ? r.fact : (all.empty() ? nullptr : all.front());
            if (!f || !f->rollback) continue; // observe-only facts are not controls
            if (r.support == cx::Support::Unknown || !r.fact) {
                block(id, r.conflict ? "support conflicting between sources" : "support unknown");
            } else if (r.support == cx::Support::No) {
                block(id, "unsupported");
            } else if (!f->readable) {
                block(id, "unreadable (never treated as writable)");
            } else if (!f->writable) {
                block(id, "not writable");
            } else if (!f->verified) {
                restricted = true;
                block(id, "writable but unverified (restricted)");
            } else if (f->risk == cx::Risk::High) {
                restricted = true;
                block(id, "verified but high risk (restricted)");
            } else {
                g.actionable.push_back({"capability", id, f->interface.empty() ? f->value : f->interface});
            }
        }
    }
    if (!g.actionable.empty()) {
        g.verdict = CapabilityGate::Verdict::Actionable;
        g.summary = std::to_string(g.actionable.size()) + " verified, writable, rollback-capable " + resource + " control(s)";
    } else if (restricted) {
        g.verdict = CapabilityGate::Verdict::Restricted;
        g.summary = resource + " controls are writable but not verified (or high risk)";
    } else {
        g.summary = g.blocking.empty() ? "no " + resource + " control capability published" : resource + " controls blocked";
        if (g.blocking.empty()) block(resource, g.summary);
    }
    return g;
}

} // namespace flux::policy
