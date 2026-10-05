#include "DecisionEngine.hpp"

#include <cstdio>
#include <exception>

namespace flux::policy {

namespace {

namespace b = flux::bottleneck;
namespace cx = flux::context;
using flux::runtime::TxState;

cx::Confidence weaker(cx::Confidence a, cx::Confidence c) { return a < c ? a : c; }

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

const char *tx_name(TxState s) { return flux::runtime::to_string(s); }

std::string fnv_hex(const std::string &s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

enum class Thermal { Safety, Boost, Unknown };

struct Builder {
    PolicyDecision d;
    void constrain(ConstraintKind k, const std::string &subject, const std::string &detail) {
        d.constraints.push_back({k, subject, detail});
    }
    void support(const std::string &src, const std::string &ref, const std::string &value) {
        d.supporting_evidence.push_back({src, ref, value});
    }
    void blocking(const std::string &src, const std::string &ref, const std::string &value) {
        d.blocking_evidence.push_back({src, ref, value});
    }
};

void cite_fps(Builder &x, const FpsEvidence &f) {
    if (f.target_refresh_hz) x.support("fps", "target_refresh_hz", num(*f.target_refresh_hz));
    if (f.observed_fps) x.support("fps", "observed_fps", num(*f.observed_fps));
    if (f.shortfall_samples && f.fps_samples)
        x.support("fps", "shortfall_samples", std::to_string(*f.shortfall_samples) + "/" + std::to_string(*f.fps_samples));
}

std::string fps_line(const FpsEvidence &f) {
    auto opt = [](const std::optional<double> &v, const char *unit) { return v ? num(*v) + " " + unit : std::string("unknown"); };
    return "fps evidence: target refresh " + opt(f.target_refresh_hz, "Hz") + ", refresh capability " +
           opt(f.refresh_capability_hz, "Hz") + ", observed " + opt(f.observed_fps, "FPS") + ", shortfall " +
           (f.shortfall_samples && f.fps_samples
                ? std::to_string(*f.shortfall_samples) + "/" + std::to_string(*f.fps_samples) + " samples"
                : std::string("unknown")) +
           " (refresh is not frame rate)";
}

void finish(Builder &x, const PolicyInputs &in) {
    x.d.limitations.push_back(fps_line(in.fps));
    x.d.limitations.push_back("decision only: no executor exists; nothing on the device was changed");
    x.d.decision_id.clear();
    x.d.decision_id = "pd-" + std::to_string(in.evaluation_time_ms) + "-" + fnv_hex(explain(x.d));
}

PolicyDecision decide(const PolicyInputs &in) {
    Builder x;
    x.d.session_id = in.session_id;
    x.d.evaluation_time_ms = in.evaluation_time_ms;
    x.support("profile", "mode", to_string(in.profile.mode));

    // -- thermal: only Synrei's verified state counts; never temperature ----------------------
    Thermal thermal = Thermal::Unknown;
    cx::Confidence thermal_conf = cx::Confidence::None;
    if (!in.thermal) {
        x.d.limitations.push_back("no thermal context: not assumed relaxed or unconstrained");
    } else if (!in.thermal->readable || !in.thermal->verified) {
        x.d.limitations.push_back("thermal context not current: " +
                                  (in.thermal->note.empty() ? std::string("unverified") : in.thermal->note));
    } else if (in.thermal->constraint == flux::thermal::Constraint::Constrained) {
        thermal = Thermal::Safety;
        thermal_conf = in.thermal->confidence;
    } else if (in.thermal->constraint == flux::thermal::Constraint::Unconstrained) {
        thermal = Thermal::Boost;
        thermal_conf = in.thermal->confidence;
    } else {
        x.d.limitations.push_back("Synrei state '" + in.thermal->state + "' does not report a constraint");
    }
    if (thermal == Thermal::Unknown)
        x.constrain(ConstraintKind::ThermalUnknown, "thermal", "thermal state unknown: stronger actions not justified");
    else
        x.support("thermal", in.thermal->source, in.thermal->state);

    // -- 1. RESTORE: existing runtime / transaction state requires it ------------------------
    const auto &rt = in.runtime;
    std::vector<std::string> restore;
    if (rt.transaction == TxState::Failed) restore.push_back("transaction failed (rollback incomplete)");
    if (rt.transaction == TxState::Restoring) restore.push_back("transaction restore incomplete");
    if (rt.transaction == TxState::Active && !rt.game_active) restore.push_back("transaction active without an active game");
    if (rt.recovery_failed) restore.push_back("journal recovery left entries unrestored");
    if (thermal == Thermal::Safety && rt.transaction == TxState::Active && rt.game_active) {
        restore.push_back("Synrei reports safety while performance changes are applied");
        x.constrain(ConstraintKind::ThermalSafety, "boost", "thermal safety overrides performance intent");
        x.blocking("thermal", in.thermal->source, in.thermal->state);
    }
    if (!restore.empty()) {
        x.d.action = Action::Restore;
        x.d.confidence = cx::Confidence::High;
        x.d.target = "transaction";
        x.support("runtime", "transaction_state", tx_name(rt.transaction));
        if (!rt.transaction_id.empty()) x.support("runtime", "transaction_id", rt.transaction_id);
        if (rt.recovery_failed) x.support("runtime", "recovery", "failed");
        std::string why;
        for (const auto &r : restore) why += (why.empty() ? "" : "; ") + r;
        x.constrain(ConstraintKind::RestoreRequired, rt.transaction_id.empty() ? "transaction" : rt.transaction_id, why);
        x.d.reason = "Restore takes priority: " + why + ".";
        finish(x, in);
        return x.d;
    }

    // -- 2. NO_ACTION: no game, nothing to restore -------------------------------------------
    if (!rt.game_active) {
        x.d.action = Action::NoAction;
        x.d.confidence = cx::Confidence::High;
        x.d.reason = "No active game session and no transaction requiring restoration.";
        x.support("runtime", "game_active", "false");
        finish(x, in);
        return x.d;
    }

    const auto *res = in.bottleneck ? &*in.bottleneck : nullptr;
    auto target_of = [](b::Kind k) {
        switch (k) {
        case b::Kind::Cpu: return std::string("cpu");
        case b::Kind::Gpu: return std::string("gpu");
        case b::Kind::Memory: return std::string("memory");
        case b::Kind::Storage: return std::string("storage");
        case b::Kind::Display: return std::string("display");
        case b::Kind::Thermal: return std::string("thermal");
        case b::Kind::Unknown: break;
        }
        return std::string();
    };
    auto observe = [&](cx::Confidence c, const std::string &reason) {
        x.d.action = Action::Observe;
        x.d.confidence = c;
        x.d.reason = reason;
        finish(x, in);
        return x.d;
    };
    auto cite_primary = [&]() {
        if (!res) return;
        for (const auto &e : res->primary.evidence) x.support("bottleneck", e.metric, e.value);
        x.support("bottleneck", "primary", std::string(b::to_string(res->primary.kind)) + " " + b::to_string(res->primary.rating));
    };
    auto apply_gate = [&](const CapabilityGate &g, const std::string &target) {
        for (const auto &e : g.actionable) x.support(e.source, e.ref, e.value);
        for (const auto &e : g.blocking) x.blocking(e.source, e.ref, e.value);
        if (g.verdict == CapabilityGate::Verdict::Restricted) x.constrain(ConstraintKind::CapabilityRestricted, target, g.summary);
        if (g.verdict == CapabilityGate::Verdict::Blocked) x.constrain(ConstraintKind::CapabilityBlocked, target, g.summary);
        return g.verdict == CapabilityGate::Verdict::Actionable;
    };

    // -- 3. Thermal safety: BOOST forbidden; MITIGATE only through an actionable control ------
    if (thermal == Thermal::Safety) {
        x.constrain(ConstraintKind::ThermalSafety, "boost", "thermal safety overrides performance intent");
        x.blocking("thermal", in.thermal->source, in.thermal->state);
        std::string target = res ? target_of(res->primary.kind) : "";
        if (target != "cpu" && target != "gpu") target = "cpu";
        cite_primary();
        x.d.target = target;
        if (apply_gate(gate(in.capabilities, target), target)) {
            x.d.action = Action::Mitigate;
            x.d.confidence = thermal_conf;
            x.d.reason = "Synrei reports safety; mitigation of " + target + " load is consistent with the thermal constraint.";
            finish(x, in);
            return x.d;
        }
        return observe(cx::Confidence::Medium, "Synrei reports safety, but no verified " + target +
                                                   " control permits mitigation; observing.");
    }

    // -- 4.-7. Bottleneck evidence (consumed as produced, never re-rated) ---------------------
    if (!res) {
        x.constrain(ConstraintKind::InsufficientEvidence, "bottleneck", "no bottleneck result available");
        return observe(cx::Confidence::Low, "No bottleneck evidence; gathering evidence.");
    }
    x.support("bottleneck", "samples", std::to_string(res->samples));
    if (res->conflict) {
        for (const auto &f : res->secondary)
            for (const auto &e : f.evidence) x.blocking("bottleneck", e.metric, e.value);
        x.constrain(ConstraintKind::Conflict, "bottleneck", res->note.empty() ? "competing categories" : res->note);
        return observe(cx::Confidence::Low, "Bottleneck evidence is conflicting; no category is acted on.");
    }
    if (res->primary.kind == b::Kind::Unknown || res->primary.rating < b::State::Likely) {
        x.constrain(ConstraintKind::InsufficientEvidence, "bottleneck",
                    res->primary.kind == b::Kind::Unknown ? (res->note.empty() ? "no primary finding" : res->note)
                                                          : "primary rated below likely");
        cite_primary();
        return observe(cx::Confidence::Low, "Bottleneck evidence is insufficient for an action.");
    }
    cite_primary();
    cite_fps(x, in.fps);
    const std::string target = target_of(res->primary.kind);
    x.d.target = target;
    const auto confidence = weaker(res->primary.confidence, thermal == Thermal::Unknown ? cx::Confidence::Low : thermal_conf);

    // -- 8. History of restore failures blocks stronger actions ------------------------------
    for (const auto &p : in.history.patterns)
        if (p.name == "repeated_restore_failure") {
            x.constrain(ConstraintKind::History, "restore", "restore failed in " + std::to_string(p.occurrences) +
                                                                " session(s) of the history window");
            x.blocking("history", p.name, std::to_string(p.occurrences));
            return observe(confidence, "Repeated restore failures in recent history; stronger actions are withheld.");
        }
    if (target == "thermal")
        return observe(confidence, "The assessment reports a thermal limit, but Synrei does not currently report safety.");
    if (thermal == Thermal::Unknown)
        return observe(weaker(confidence, cx::Confidence::Low),
                       target + " bottleneck rated " + b::to_string(res->primary.rating) +
                           ", but the thermal state is unknown; observing.");

    // -- 9. Synrei boost: BOOST only with full evidence, otherwise OBSERVE (never MITIGATE) ------------
    const bool confirmed = res->primary.rating == b::State::Confirmed;
    bool boost_ok = target == "cpu" || target == "gpu";
    if (boost_ok && !confirmed) {
        boost_ok = false;
        x.constrain(ConstraintKind::InsufficientEvidence, "boost", "bottleneck not confirmed");
    }
    if (boost_ok && !in.fps.shortfall_observed()) {
        boost_ok = false;
        x.constrain(ConstraintKind::InsufficientEvidence, "boost", "no observed FPS shortfall");
    }
    if (boost_ok && !in.profile.requests_performance()) {
        boost_ok = false;
        x.constrain(ConstraintKind::ProfileIntent, "boost",
                    std::string("profile '") + to_string(in.profile.mode) + "' does not request performance");
    }
    if (!apply_gate(gate(in.capabilities, target), target))
        return observe(weaker(confidence, cx::Confidence::Medium),
                       target + " bottleneck rated " + b::to_string(res->primary.rating) +
                           ", but no verified writable control permits an action.");
    // B-38: MITIGATE lowers a ceiling. Under Synrei boost a confirmed CPU/GPU bottleneck is
    // the opposite of a reason to lower it, so anything short of full BOOST evidence observes.
    if (!boost_ok)
        return observe(weaker(confidence, cx::Confidence::Medium),
                       target + " bottleneck rated " + b::to_string(res->primary.rating) +
                           " under Synrei boost, but BOOST requirements are not all met; MITIGATE would "
                           "lower the ceiling of the limiting resource, so observing.");
    x.d.action = Action::Boost;
    x.d.confidence = confidence;
    x.d.reason = target + " bottleneck confirmed with an observed FPS shortfall, Synrei reports boost, and a "
                          "verified control exists; BOOST is supported by the evidence.";
    finish(x, in);
    return x.d;
}

} // namespace

PolicyDecision DecisionEngine::evaluate(const PolicyInputs &in) const {
    try {
        return decide(in);
    } catch (const std::exception &e) {
        PolicyDecision d;
        d.session_id = in.session_id;
        d.evaluation_time_ms = in.evaluation_time_ms;
        d.action = Action::Observe;
        d.reason = "Decision evaluation failed; observing.";
        d.limitations.push_back(std::string("evaluation failed: ") + e.what());
        d.decision_id = "pd-" + std::to_string(in.evaluation_time_ms) + "-" + fnv_hex(explain(d));
        return d;
    } catch (...) {
        PolicyDecision d;
        d.session_id = in.session_id;
        d.evaluation_time_ms = in.evaluation_time_ms;
        d.action = Action::Observe;
        d.reason = "Decision evaluation failed; observing.";
        d.limitations.push_back("evaluation failed: unknown error");
        d.decision_id = "pd-" + std::to_string(in.evaluation_time_ms) + "-" + fnv_hex(explain(d));
        return d;
    }
}

} // namespace flux::policy
