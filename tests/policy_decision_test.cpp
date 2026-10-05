// Decision & policy foundation (Step 8.13): evidence-first decisions, capability gating, safety
// hierarchy, restore priority, determinism, exact evidence, no execution.
#include "flux_test.hpp"
#include "DecisionEngine.hpp"

#include <set>

namespace b = flux::bottleneck;
namespace ctx = flux::context;
namespace o = flux::observatory;
namespace p = flux::policy;
namespace t = flux::thermal;
using flux::runtime::TxState;

namespace {

ctx::CapabilityFact control(const std::string &id, const std::string &domain, bool verified = true,
                            ctx::Support support = ctx::Support::Yes, bool readable = true, bool writable = true,
                            ctx::Risk risk = ctx::Risk::Medium) {
    ctx::CapabilityFact f;
    f.id = id;
    f.domain = domain;
    f.source = "generic";
    f.support = support;
    f.readable = readable;
    f.writable = writable;
    f.verified = verified;
    f.rollback = true;
    f.risk = risk;
    f.confidence = ctx::Confidence::High;
    f.interface = "sys/" + id;
    f.value = "1";
    return f;
}

ctx::CapabilityContext caps(std::vector<ctx::CapabilityFact> facts) {
    ctx::CapabilityContext c;
    c.publish("kernel", std::move(facts));
    return c;
}

b::BottleneckResult result(b::Kind kind, b::State rating, ctx::Confidence conf, bool conflict = false) {
    b::BottleneckResult r;
    r.session_id = "s-1";
    r.timestamp_ms = 9000;
    r.samples = 6;
    r.conflict = conflict;
    if (!conflict) {
        r.primary.kind = kind;
        r.primary.rating = rating;
        r.primary.confidence = conf;
        r.primary.source = "session";
        r.primary.timestamp_ms = 9000;
        r.primary.evidence = {{std::string(b::to_string(kind)) + "_busy", "peak 0.97, 6/6 samples", ">= 0.90", "session", 6000},
                              {"frame_deficit", "6/6 samples below target", "fps < target * 0.90", "session", 6000}};
    } else {
        r.note = "conflict: cpu gpu";
        b::Finding cpu{b::Kind::Cpu, b::State::Likely, ctx::Confidence::Medium, "session", 9000,
                       {{"cpu_busiest_core", "peak 0.97, 6/6 samples", ">= 0.90", "session", 6000}}, ""};
        b::Finding gpu{b::Kind::Gpu, b::State::Likely, ctx::Confidence::Medium, "session", 9000,
                       {{"gpu_busy", "peak 0.97, 6/6 samples", ">= 0.90", "session", 6000}}, ""};
        r.secondary = {cpu, gpu};
    }
    return r;
}

t::ThermalSnapshot synrei(const std::string &state, bool verified = true) {
    t::ThermalSnapshot s;
    s.timestamp_ms = 9000;
    s.source = "synrei:/dev/hico/state";
    s.state = state;
    s.readable = true;
    s.verified = verified;
    s.confidence = verified ? ctx::Confidence::High : ctx::Confidence::None;
    if (verified && state == "safety") s.constraint = t::Constraint::Constrained;
    else if (verified && state == "boost") s.constraint = t::Constraint::Unconstrained;
    if (!verified) s.note = "stale: Synrei state is 40 s old; not current evidence";
    return s;
}

p::FpsEvidence shortfall() {
    p::FpsEvidence f;
    f.target_refresh_hz = 60;
    f.refresh_capability_hz = 120;
    f.observed_fps = 41;
    f.shortfall_samples = 6;
    f.fps_samples = 6;
    f.source = "fps_observation";
    return f;
}

p::PolicyInputs base(const ctx::CapabilityContext *c) {
    p::PolicyInputs in;
    in.session_id = "s-1";
    in.evaluation_time_ms = 10000;
    in.capabilities = c;
    in.runtime.game_active = true;
    in.runtime.transaction = TxState::Inactive;
    return in;
}

bool has(const p::PolicyDecision &d, p::ConstraintKind k, const std::string &subject = "") {
    for (auto &c : d.constraints)
        if (c.kind == k && (subject.empty() || c.subject == subject)) return true;
    return false;
}

const p::DecisionEngine engine;

void test_no_and_unknown_evidence() {
    auto c = caps({});
    auto in = base(&c);
    auto d = engine.evaluate(in);
    CHECK(d.action == p::Action::Observe); // 1. no evidence: observe, never act
    CHECK(d.confidence == ctx::Confidence::Low);
    CHECK(has(d, p::ConstraintKind::InsufficientEvidence));
    CHECK(has(d, p::ConstraintKind::ThermalUnknown));
    in.runtime.game_active = false;
    CHECK(engine.evaluate(in).action == p::Action::NoAction); // nothing running, nothing to restore

    auto in2 = base(&c); // 2. unknown evidence stays unknown
    in2.bottleneck = result(b::Kind::Unknown, b::State::Unknown, ctx::Confidence::None);
    in2.thermal = synrei("relaxed"); // verified, but Synrei does not report a constraint
    auto d2 = engine.evaluate(in2);
    CHECK(d2.action == p::Action::Observe);
    CHECK(has(d2, p::ConstraintKind::InsufficientEvidence));
    CHECK(has(d2, p::ConstraintKind::ThermalUnknown)); // relaxed is not assumed unconstrained
}

void test_stale_thermal() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    auto in = base(&c);
    in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
    in.thermal = synrei("safety", false); // stale safety: not current evidence
    in.fps = shortfall();
    in.profile.mode = p::ProfileMode::Performance;
    auto d = engine.evaluate(in);
    CHECK(d.action == p::Action::Observe);
    CHECK(!has(d, p::ConstraintKind::ThermalSafety)); // stale state is not used as safety either
    CHECK(has(d, p::ConstraintKind::ThermalUnknown));
    CHECK(d.confidence <= ctx::Confidence::Low);
    bool cites_stale = false;
    for (auto &l : d.limitations) cites_stale = cites_stale || l.find("stale") != std::string::npos;
    CHECK(cites_stale);
}

void test_thermal_safety_and_boost() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu"), control("cpufreq.policy4.scaling_max_freq", "cpufreq")});
    auto in = base(&c);
    in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
    in.fps = shortfall();
    in.profile.mode = p::ProfileMode::Performance;

    in.thermal = synrei("safety"); // 4. safety overrides performance intent
    auto d = engine.evaluate(in);
    CHECK(d.action == p::Action::Mitigate);
    CHECK(has(d, p::ConstraintKind::ThermalSafety, "boost"));
    bool thermal_blocks = false;
    for (auto &e : d.blocking_evidence) thermal_blocks = thermal_blocks || (e.source == "thermal" && e.value == "safety");
    CHECK(thermal_blocks);
    in.runtime.transaction = TxState::Active; // performance changes applied: restore them
    auto r = engine.evaluate(in);
    CHECK(r.action == p::Action::Restore);
    CHECK(has(r, p::ConstraintKind::ThermalSafety));

    in.runtime.transaction = TxState::Inactive; // 5. Synrei boost permits considering BOOST
    in.thermal = synrei("boost");
    auto bo = engine.evaluate(in);
    CHECK(bo.action == p::Action::Boost);
    CHECK_EQ(bo.target, std::string("gpu"));
    CHECK(bo.confidence == ctx::Confidence::High);
    for (auto &w : {"boost", "safety"}) (void)w;
    // Without an FPS shortfall there is nothing to boost for: mitigate path, boost not justified.
    in.fps = {};
    auto nofps = engine.evaluate(in);
    CHECK(nofps.action != p::Action::Boost);
    CHECK(has(nofps, p::ConstraintKind::InsufficientEvidence, "boost"));
}

void test_confirmed_cpu_and_gpu() {
    auto c = caps({control("cpufreq.policy4.scaling_max_freq", "cpufreq")});
    auto in = base(&c);
    in.thermal = synrei("boost");
    in.bottleneck = result(b::Kind::Cpu, b::State::Confirmed, ctx::Confidence::High);
    in.profile.mode = p::ProfileMode::Balance;
    auto d = engine.evaluate(in); // 6. B-38: boost + confirmed CPU, BOOST unmet -> OBSERVE
    CHECK(d.action == p::Action::Observe);
    CHECK_EQ(d.target, std::string("cpu"));

    in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High); // 7. no GPU control
    auto g = engine.evaluate(in);
    CHECK(g.action == p::Action::Observe);
    CHECK(has(g, p::ConstraintKind::CapabilityBlocked, "gpu"));
}

void test_conflict() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu"), control("cpufreq.policy4.scaling_max_freq", "cpufreq")});
    auto in = base(&c);
    in.thermal = synrei("boost");
    in.fps = shortfall();
    in.profile.mode = p::ProfileMode::Performance;
    in.bottleneck = result(b::Kind::Unknown, b::State::Unknown, ctx::Confidence::None, true);
    auto d = engine.evaluate(in); // 8.
    CHECK(d.action == p::Action::Observe);
    CHECK(has(d, p::ConstraintKind::Conflict));
    CHECK(d.blocking_evidence.size() >= 2); // both competing findings preserved
    in.thermal = synrei("safety");
    in.runtime.transaction = TxState::Active;
    CHECK(engine.evaluate(in).action == p::Action::Restore); // higher-priority safety still wins
}

void test_capability_gating() {
    auto check = [](ctx::CapabilityFact f, p::CapabilityGate::Verdict want, p::Action action) {
        auto c = caps({f});
        auto g = p::gate(&c, "gpu");
        CHECK(g.verdict == want);
        auto in = base(&c);
        in.thermal = synrei("safety"); // the gate decides whether safety may MITIGATE
        in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
        auto d = p::DecisionEngine().evaluate(in);
        CHECK(d.action == action);
        return d;
    };
    // 9. unsupported, 10. unreadable, non-writable: blocked.
    auto d9 = check(control("gpu.kgsl.max_gpuclk", "gpu", true, ctx::Support::No), p::CapabilityGate::Verdict::Blocked,
                    p::Action::Observe);
    CHECK(has(d9, p::ConstraintKind::CapabilityBlocked));
    check(control("gpu.kgsl.max_gpuclk", "gpu", true, ctx::Support::Yes, false), p::CapabilityGate::Verdict::Blocked,
          p::Action::Observe);
    check(control("gpu.kgsl.max_gpuclk", "gpu", true, ctx::Support::Yes, true, false), p::CapabilityGate::Verdict::Blocked,
          p::Action::Observe);
    // Unknown support is not "supported".
    check(control("gpu.kgsl.max_gpuclk", "gpu", true, ctx::Support::Unknown), p::CapabilityGate::Verdict::Blocked,
          p::Action::Observe);
    // 11. writable but unverified: restricted, never acted on.
    auto d11 = check(control("gpu.kgsl.max_gpuclk", "gpu", false), p::CapabilityGate::Verdict::Restricted, p::Action::Observe);
    CHECK(has(d11, p::ConstraintKind::CapabilityRestricted, "gpu"));
    // High risk is not "safe": restricted.
    check(control("gpu.kgsl.max_gpuclk", "gpu", true, ctx::Support::Yes, true, true, ctx::Risk::High),
          p::CapabilityGate::Verdict::Restricted, p::Action::Observe);
    // 12. verified + writable + readable + rollback + acceptable risk: actionable.
    auto ok = check(control("gpu.kgsl.max_gpuclk", "gpu"), p::CapabilityGate::Verdict::Actionable, p::Action::Mitigate);
    // Verified evidence wins over an unverified duplicate published by another source.
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    auto weaker = control("gpu.kgsl.max_gpuclk", "gpu", false);
    weaker.confidence = ctx::Confidence::Low;
    c.publish("other", {weaker});
    CHECK(p::gate(&c, "gpu").verdict == p::CapabilityGate::Verdict::Actionable);
    CHECK(p::gate(nullptr, "gpu").verdict == p::CapabilityGate::Verdict::Blocked);
    CHECK(p::gate(&c, "display").verdict == p::CapabilityGate::Verdict::Blocked); // no control defined
    (void)ok;
}

void test_restore_priority() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    auto in = base(&c);
    in.thermal = synrei("boost");
    in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
    in.fps = shortfall();
    in.profile.mode = p::ProfileMode::Performance;
    for (auto state : {TxState::Failed, TxState::Restoring}) { // 13.
        in.runtime.transaction = state;
        auto d = engine.evaluate(in);
        CHECK(d.action == p::Action::Restore);
        CHECK(d.confidence == ctx::Confidence::High);
        CHECK(has(d, p::ConstraintKind::RestoreRequired));
    }
    in.runtime.transaction = TxState::Active;
    in.runtime.game_active = false; // session over, changes still applied
    CHECK(engine.evaluate(in).action == p::Action::Restore);
    in.runtime = {};
    in.runtime.game_active = true;
    in.runtime.recovery_failed = true;
    CHECK(engine.evaluate(in).action == p::Action::Restore);
    in.runtime.recovery_failed = false;
    in.runtime.transaction = TxState::Active; // active during the game: normal, not a restore
    CHECK(engine.evaluate(in).action == p::Action::Boost);
}

void test_profile_intent() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    auto in = base(&c);
    in.profile.mode = p::ProfileMode::Performance; // 14. intent alone
    auto d = engine.evaluate(in);
    CHECK(d.action == p::Action::Observe);
    in.thermal = synrei("boost");
    in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
    in.fps = shortfall();
    in.profile.mode = p::ProfileMode::Powersave; // evidence for boost, but the profile does not request it
    auto m = engine.evaluate(in);
    CHECK(m.action == p::Action::Observe); // B-38: never MITIGATE under boost
    CHECK(has(m, p::ConstraintKind::ProfileIntent, "boost"));
    // LIKELY (not confirmed) is not enough for boost.
    in.profile.mode = p::ProfileMode::Performance;
    in.bottleneck = result(b::Kind::Gpu, b::State::Likely, ctx::Confidence::Medium);
    auto l = engine.evaluate(in);
    CHECK(l.action == p::Action::Observe); // B-38
    CHECK(l.confidence == ctx::Confidence::Medium);
}

// B-38: MITIGATE means "lower the ceiling one step"; only Synrei safety justifies it.
void test_b38_mitigate_semantics() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu"), control("cpufreq.policy4.scaling_max_freq", "cpufreq")});
    for (auto kind : {b::Kind::Cpu, b::Kind::Gpu}) {
        auto in = base(&c);
        in.thermal = synrei("boost");
        in.bottleneck = result(kind, b::State::Confirmed, ctx::Confidence::High);
        in.profile.mode = p::ProfileMode::Balance; // BOOST unmet (profile, FPS)
        auto d = engine.evaluate(in);
        CHECK(d.action == p::Action::Observe); // boost + CPU/GPU -> OBSERVE, never MITIGATE
        in.profile.mode = p::ProfileMode::Performance;
        CHECK(engine.evaluate(in).action == p::Action::Observe); // missing FPS -> no BOOST
        in.fps = shortfall();
        CHECK(engine.evaluate(in).action == p::Action::Boost); // full requirements -> BOOST
    }
    auto in = base(&c);
    in.thermal = synrei("safety");
    in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
    CHECK(engine.evaluate(in).action == p::Action::Mitigate); // safety + verified -> MITIGATE
    auto u = caps({control("gpu.kgsl.max_gpuclk", "gpu", false)});
    in.capabilities = &u;
    CHECK(engine.evaluate(in).action == p::Action::Observe); // safety + unverified -> OBSERVE
    for (auto st : {"relaxed", "idle", "suspended", "disabled"}) { // non-constraining, not "relaxed" evidence
        auto r = base(&c);
        r.thermal = synrei(st);
        r.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
        r.fps = shortfall();
        r.profile.mode = p::ProfileMode::Performance;
        auto d = engine.evaluate(r);
        CHECK(d.action == p::Action::Observe);
        CHECK(has(d, p::ConstraintKind::ThermalUnknown));
    }
}

void test_determinism_and_evidence() {
    auto c1 = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    auto c2 = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    auto mk = [&](const ctx::CapabilityContext *c) {
        auto in = base(c);
        in.thermal = synrei("boost");
        in.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
        in.fps = shortfall();
        in.profile.mode = p::ProfileMode::Performance;
        return in;
    };
    auto a = engine.evaluate(mk(&c1)), a2 = engine.evaluate(mk(&c1)), b2 = engine.evaluate(mk(&c2));
    CHECK_EQ(p::explain(a), p::explain(a2)); // 15.
    CHECK_EQ(a.decision_id, b2.decision_id); // 16. identical inputs, identical decision
    CHECK_EQ(p::explain(a), p::explain(b2));
    CHECK(a.decision_id.rfind("pd-", 0) == 0);
    auto other = mk(&c1);
    other.profile.mode = p::ProfileMode::Balance;
    CHECK(engine.evaluate(other).decision_id != a.decision_id);
    // 17. exact references: bottleneck evidence and capability ids copied verbatim.
    bool bottleneck_ref = false, cap_ref = false, fps_ref = false;
    for (auto &e : a.supporting_evidence) {
        bottleneck_ref = bottleneck_ref || (e.source == "bottleneck" && e.ref == "frame_deficit" &&
                                            e.value == "6/6 samples below target");
        cap_ref = cap_ref || (e.source == "capability" && e.ref == "gpu.kgsl.max_gpuclk");
        fps_ref = fps_ref || (e.source == "fps" && e.ref == "shortfall_samples" && e.value == "6/6");
    }
    CHECK(bottleneck_ref && cap_ref && fps_ref);
    auto text = p::explain(a);
    for (auto bad : {"definitely", "guaranteed", "proven", "caused by"}) CHECK(text.find(bad) == std::string::npos);
    CHECK(text.find("not executed") != std::string::npos); // decision != execution
    CHECK(text.find("60.00 Hz") != std::string::npos && text.find("41.00 FPS") != std::string::npos);
}

void test_no_writes_and_isolation() {
    auto c = caps({control("gpu.kgsl.max_gpuclk", "gpu")});
    const auto generation = c.generation();
    const auto before = c.facts("gpu.kgsl.max_gpuclk")[0]->value;
    auto in = base(&c);
    in.thermal = synrei("safety");
    in.runtime.transaction = TxState::Active;
    const auto runtime_before = in.runtime;
    auto d = engine.evaluate(in); // 18. pure: capability context and inputs untouched
    CHECK(c.generation() == generation);
    CHECK_EQ(c.facts("gpu.kgsl.max_gpuclk")[0]->value, before);
    CHECK(in.runtime.transaction == runtime_before.transaction && in.runtime.game_active == runtime_before.game_active);
    CHECK(d.action == p::Action::Restore); // a recommendation only: nothing was restored here
    CHECK(in.runtime.transaction == TxState::Active);

    // 19. analysis unavailable (empty history) or a failure pattern never alters runtime state,
    //     and a history of restore failures blocks stronger actions.
    auto in2 = base(&c);
    in2.thermal = synrei("boost");
    in2.bottleneck = result(b::Kind::Gpu, b::State::Confirmed, ctx::Confidence::High);
    o::Pattern restore;
    restore.name = "repeated_restore_failure";
    restore.occurrences = 2;
    in2.history.patterns = {restore};
    auto h = engine.evaluate(in2);
    CHECK(h.action == p::Action::Observe);
    CHECK(has(h, p::ConstraintKind::History));
    p::PolicyInputs broken; // null capability context, nothing else
    auto bd = engine.evaluate(broken);
    CHECK(bd.action == p::Action::NoAction || bd.action == p::Action::Observe);
}

void test_b35_unchanged() {
    // 20. No telemetry expansion: no new event types, schema v1.
    CHECK_EQ(o::kSchemaVersion, 1);
    auto reg = o::EventRegistry::builtin();
    CHECK_EQ(reg.types().size(), size_t(19));
    for (auto &[name, _] : reg.types()) CHECK(name.rfind("POLICY", 0) != 0 && name.rfind("DECISION", 0) != 0);
    CHECK_EQ(std::string(p::to_string(p::Action::NoAction)), std::string("NO_ACTION"));
    std::set<std::string> actions;
    for (auto a : {p::Action::NoAction, p::Action::Observe, p::Action::Mitigate, p::Action::Boost, p::Action::Restore})
        actions.insert(p::to_string(a));
    CHECK_EQ(actions.size(), size_t(5));
}

} // namespace

int main() {
    test_no_and_unknown_evidence();
    test_stale_thermal();
    test_thermal_safety_and_boost();
    test_confirmed_cpu_and_gpu();
    test_conflict();
    test_capability_gating();
    test_restore_priority();
    test_profile_intent();
    test_b38_mitigate_semantics();
    test_determinism_and_evidence();
    test_no_writes_and_isolation();
    test_b35_unchanged();
    return flux_test::report("policy_decision_test");
}
