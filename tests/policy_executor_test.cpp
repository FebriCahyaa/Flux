// Controlled Policy Executor (Phase 4B): action handling, capability gating, trusted operations,
// thermal / runtime validation, Transaction Engine as the only write path, failures, rollback,
// restore, idempotency, Observatory compatibility, determinism, isolation.
#include "flux_test.hpp"
#include "DecisionEngine.hpp"
#include "EventStore.hpp"
#include "ObservatoryBridge.hpp"
#include "PolicyExecutor.hpp"

#include <set>

namespace ctx = flux::context;
namespace o = flux::observatory;
namespace p = flux::policy;
namespace rt = flux::runtime;
namespace t = flux::thermal;
using rt::TxState;

namespace {

struct FakeNodes {
    std::map<std::string, std::string> nodes;
    std::set<std::string> reject, sticky;
    bool reject_all = false;
    std::vector<std::pair<std::string, std::string>> written;
    rt::Io io() {
        rt::Io io;
        io.exists = [this](const std::string &q) { return nodes.count(q) > 0; };
        io.read = [this](const std::string &q) -> std::optional<std::string> {
            auto it = nodes.find(q);
            if (it == nodes.end()) return std::nullopt;
            return it->second + "\n";
        };
        io.write = [this](const std::string &q, const std::string &v) {
            written.emplace_back(q, v);
            if (!nodes.count(q) || reject_all || reject.count(q)) return false;
            if (!sticky.count(q)) nodes[q] = v;
            return true;
        };
        return io;
    }
};

const std::string P0 = "sys/devices/system/cpu/cpufreq/policy0/", P4 = "sys/devices/system/cpu/cpufreq/policy4/";
const std::string MAX0 = "/" + P0 + "scaling_max_freq", MAX4 = "/" + P4 + "scaling_max_freq";
const std::string KGSL = "/sys/class/kgsl/kgsl-3d0/max_gpuclk";

FakeNodes device() {
    FakeNodes f;
    f.nodes[MAX0] = "1804800";
    f.nodes["/" + P0 + "scaling_min_freq"] = "300000";
    f.nodes["/" + P0 + "cpuinfo_max_freq"] = "1804800";
    f.nodes["/" + P0 + "scaling_available_frequencies"] = "300000 1036800 1497600 1804800";
    f.nodes[MAX4] = "2016000";
    f.nodes["/" + P4 + "scaling_min_freq"] = "710400";
    f.nodes["/" + P4 + "cpuinfo_max_freq"] = "2419200";
    f.nodes["/" + P4 + "scaling_available_frequencies"] = "710400 1574400 2016000 2419200";
    f.nodes[KGSL] = "441600000";
    f.nodes["/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies"] = "587000000 525000000 441600000 305000000";
    f.nodes["/proc/sys/vm/swappiness"] = "60";
    return f;
}

ctx::CapabilityFact control(const std::string &id, const std::string &domain, const std::string &interface,
                            bool verified = true) {
    ctx::CapabilityFact f;
    f.id = id;
    f.domain = domain;
    f.source = "generic";
    f.support = ctx::Support::Yes;
    f.readable = true;
    f.writable = true;
    f.verified = verified;
    f.rollback = true;
    f.risk = ctx::Risk::Medium;
    f.confidence = ctx::Confidence::High;
    f.interface = interface;
    f.value = "v";
    f.range = "r";
    return f;
}

ctx::CapabilityContext cpu_caps(bool verified = true) {
    ctx::CapabilityContext c;
    c.publish("kernel", {control("cpufreq.policy0.scaling_max_freq", "cpufreq", P0 + "scaling_max_freq", verified),
                         control("cpufreq.policy4.scaling_max_freq", "cpufreq", P4 + "scaling_max_freq", verified)});
    return c;
}

ctx::CapabilityContext gpu_caps() {
    ctx::CapabilityContext c;
    c.publish("kernel", {control("gpu.kgsl.max_gpuclk", "gpu", "sys/class/kgsl/kgsl-3d0/max_gpuclk")});
    return c;
}

p::PolicyDecision decision(p::Action a, const std::string &target, const std::string &id = "pd-1") {
    p::PolicyDecision d;
    d.decision_id = id;
    d.action = a;
    d.target = target;
    d.session_id = "s-1";
    d.evaluation_time_ms = 1000;
    d.confidence = ctx::Confidence::High;
    d.reason = "test";
    return d;
}

p::ExecutionContext context(const ctx::CapabilityContext *c) {
    p::ExecutionContext x;
    x.session_id = "s-1";
    x.runtime.game_active = true;
    x.runtime.transaction = TxState::Active; // GameRuntime's own per-game transaction: normal
    x.capabilities = c;
    return x;
}

bool blocked_by(const p::PolicyExecutionResult &r, const std::string &needle) {
    for (auto &b : r.blocked_constraints)
        if (b.find(needle) != std::string::npos) return true;
    return false;
}

void expect_blocked(const p::PolicyExecutionResult &r, const std::string &reason, const FakeNodes &n) {
    CHECK(r.final_status == p::ExecutionStatus::Blocked);
    CHECK(!r.executed && !r.verified);
    CHECK(blocked_by(r, reason));
    CHECK(n.written.empty()); // nothing reached a node
    CHECK(r.transaction_id.empty());
}

void test_no_action_observe() {
    auto n = device();
    auto c = cpu_caps();
    p::PolicyExecutor ex(n.io());
    auto r1 = ex.execute(decision(p::Action::NoAction, ""), context(&c)); // 1
    auto r2 = ex.execute(decision(p::Action::Observe, "cpu"), context(&c)); // 2
    for (auto &r : {r1, r2}) {
        CHECK(r.final_status == p::ExecutionStatus::NotExecuted && !r.executed && r.transaction_id.empty());
    }
    CHECK(n.written.empty());
    // 15. OBSERVE stays OBSERVE even with every capability verified and actionable.
    CHECK(!ex.active());
}

void test_mitigate_and_boost() {
    { // 3. verified MITIGATE: each verified policy one listed step lower, via one transaction
        auto n = device();
        auto c = cpu_caps();
        std::vector<std::string> journal;
        p::PolicyExecutor ex(n.io(), [&](const std::string &j) {
            journal.push_back(j);
            return true;
        });
        auto r = ex.execute(decision(p::Action::Mitigate, "cpu"), context(&c));
        CHECK(r.final_status == p::ExecutionStatus::Applied);
        CHECK(r.executed && r.verified && !r.rolled_back);
        CHECK(!r.transaction_id.empty());
        CHECK_EQ(n.nodes[MAX0], std::string("1497600"));
        CHECK_EQ(n.nodes[MAX4], std::string("1574400"));
        CHECK_EQ(r.affected_capabilities.size(), size_t(2));
        CHECK(r.affected_capabilities[0].before == "1804800" && r.affected_capabilities[0].target == "1497600" &&
              r.affected_capabilities[0].after == "1497600");
        // 18. writes came only through the Transaction Engine: write-ahead journal with originals first.
        auto first = rt::journal::parse(journal.front());
        CHECK(first.domain == "policy" && first.subject == "pd-1");
        CHECK(first.entries.size() == 1 && first.entries[0] == rt::journal::encode_entry(MAX0, "1804800"));
        CHECK_EQ(n.written.size(), size_t(2));
        // RESTORE through the same transaction.
        auto rs = ex.execute(decision(p::Action::Restore, "transaction", "pd-2"), context(&c));
        CHECK(rs.final_status == p::ExecutionStatus::Restored && rs.restored);
        CHECK(n.nodes[MAX0] == "1804800" && n.nodes[MAX4] == "2016000");
        CHECK(rt::journal::parse(journal.back()).entries.empty());
        CHECK(!ex.active());
    }
    { // 4. verified BOOST: highest listed frequency, never above the known maximum
        auto n = device();
        auto c = gpu_caps();
        p::PolicyExecutor ex(n.io());
        auto r = ex.execute(decision(p::Action::Boost, "gpu"), context(&c));
        CHECK(r.final_status == p::ExecutionStatus::Applied && r.executed && r.verified);
        CHECK_EQ(n.nodes[KGSL], std::string("587000000"));
        // Already at the limit: no change, nothing executed.
        auto n2 = device();
        n2.nodes[KGSL] = "587000000";
        p::PolicyExecutor ex2(n2.io());
        auto r2 = ex2.execute(decision(p::Action::Boost, "gpu"), context(&c));
        CHECK(r2.final_status == p::ExecutionStatus::NoChange && !r2.executed && n2.written.empty());
    }
}

void test_capability_gating() {
    struct Case {
        std::function<void(ctx::CapabilityFact &)> change;
        std::string reason;
    };
    const std::vector<Case> cases = {
        {[](ctx::CapabilityFact &f) { f.verified = false; }, "capability_unverified"},         // 5
        {[](ctx::CapabilityFact &f) { f.support = ctx::Support::No; }, "capability_unsupported"}, // 6
        {[](ctx::CapabilityFact &f) { f.readable = false; }, "capability_unreadable"},          // 7
        {[](ctx::CapabilityFact &f) { f.writable = false; }, "capability_not_writable"},        // 8
        {[](ctx::CapabilityFact &f) { f.risk = ctx::Risk::High; }, "unsafe_risk"},              // 9
        {[](ctx::CapabilityFact &f) { f.risk = ctx::Risk::Unknown; }, "unsafe_risk"},
        {[](ctx::CapabilityFact &f) { f.rollback = false; }, "no_rollback"},                    // 10
    };
    for (const auto &k : cases) {
        auto n = device();
        auto f = control("gpu.kgsl.max_gpuclk", "gpu", "sys/class/kgsl/kgsl-3d0/max_gpuclk");
        k.change(f);
        ctx::CapabilityContext c;
        c.publish("kernel", {f});
        p::PolicyExecutor ex(n.io());
        auto r = ex.execute(decision(p::Action::Mitigate, "gpu"), context(&c));
        expect_blocked(r, k.reason, n);
        CHECK(blocked_by(r, "gpu.kgsl.max_gpuclk")); // exact capability named
    }
    { // 11. invalid target: no listed frequencies to choose from (nothing invented)
        auto n = device();
        n.nodes.erase("/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies");
        auto c = gpu_caps();
        p::PolicyExecutor ex(n.io());
        expect_blocked(ex.execute(decision(p::Action::Mitigate, "gpu"), context(&c)), "invalid_target", n);
        // Current value outside the listed range.
        auto n2 = device();
        n2.nodes[KGSL] = "999999999";
        p::PolicyExecutor ex2(n2.io());
        expect_blocked(ex2.execute(decision(p::Action::Boost, "gpu"), context(&c)), "invalid_target", n2);
        // Unknown target resource.
        p::PolicyExecutor ex3(n.io());
        expect_blocked(ex3.execute(decision(p::Action::Mitigate, "../../etc"), context(&c)), "invalid_target", n);
        expect_blocked(ex3.execute(decision(p::Action::Mitigate, "memory"), context(&c)), "no_trusted_adapter", n);
    }
    { // 24. arbitrary paths are never executed: only trusted operation patterns become operations
        auto n = device();
        ctx::CapabilityContext c;
        c.publish("kernel", {control("cpufreq.evil", "cpufreq", "/etc/passwd"),
                             control("cpufreq.trav", "cpufreq", P0 + "../../../../proc/sys/kernel/x"),
                             control("cpufreq.gov", "cpufreq", P0 + "scaling_governor"),
                             control("swap.swappiness", "swap", "proc/sys/vm/swappiness")});
        p::PolicyExecutor ex(n.io());
        auto r = ex.execute(decision(p::Action::Mitigate, "cpu"), context(&c));
        expect_blocked(r, "no_trusted_adapter", n);
        // A thermal fact in a control domain is never an operation.
        ctx::CapabilityContext th;
        th.publish("kernel", {control("cpufreq.cool", "cpufreq", "sys/class/thermal/cooling_device0/cur_state")});
        expect_blocked(ex.execute(decision(p::Action::Mitigate, "cpu"), context(&th)), "no_trusted_adapter", n);
    }
}

void test_thermal_profile_and_integrity() {
    auto n = device();
    auto c = gpu_caps();
    p::PolicyExecutor ex(n.io());
    // 12. Synrei safety (verified) blocks BOOST even if a BOOST decision arrives.
    auto x = context(&c);
    t::ThermalSnapshot safety;
    safety.readable = safety.verified = true;
    safety.state = "safety";
    safety.constraint = t::Constraint::Constrained;
    x.thermal = safety;
    expect_blocked(ex.execute(decision(p::Action::Boost, "gpu"), x), "thermal_safety", n);
    // A decision carrying a thermal-safety constraint for BOOST is refused as well.
    auto d = decision(p::Action::Boost, "gpu");
    d.constraints.push_back({p::ConstraintKind::ThermalSafety, "boost", "safety"});
    expect_blocked(ex.execute(d, context(&c)), "decision_constraint", n);
    // 13. stale / unknown thermal is not reinterpreted: no safety invented, no boost refused for it;
    //     a decision that says thermal is unknown cannot be executed as MITIGATE/BOOST.
    auto stale = context(&c);
    t::ThermalSnapshot s;
    s.readable = true;
    s.verified = false;
    s.state = "safety";
    stale.thermal = s;
    auto ok = ex.execute(decision(p::Action::Boost, "gpu", "pd-stale"), stale);
    CHECK(ok.final_status == p::ExecutionStatus::Applied); // the approved decision stands
    ex.execute(decision(p::Action::Restore, "transaction", "pd-r"), stale);
    auto unknown = decision(p::Action::Mitigate, "gpu", "pd-u");
    unknown.constraints.push_back({p::ConstraintKind::ThermalUnknown, "thermal", "unknown"});
    auto n2 = device();
    p::PolicyExecutor ex2(n2.io());
    expect_blocked(ex2.execute(unknown, context(&c)), "decision_constraint", n2);

    // 14. Profile intent alone never executes: the Decision Engine says OBSERVE, the executor obeys.
    p::PolicyInputs in;
    in.session_id = "s-1";
    in.capabilities = &c;
    in.runtime.game_active = true;
    in.profile.mode = p::ProfileMode::Performance;
    const auto observe = p::DecisionEngine().evaluate(in);
    CHECK(observe.action == p::Action::Observe);
    auto n3 = device();
    p::PolicyExecutor ex3(n3.io());
    CHECK(ex3.execute(observe, context(&c)).final_status == p::ExecutionStatus::NotExecuted);
    CHECK(n3.written.empty());

    // 16. MITIGATE is executed as MITIGATE (one step down), never as BOOST.
    auto n4 = device();
    p::PolicyExecutor ex4(n4.io());
    auto m = ex4.execute(decision(p::Action::Mitigate, "gpu"), context(&c));
    CHECK(m.requested_action == p::Action::Mitigate && n4.nodes[KGSL] == "305000000");

    // Runtime state validation.
    auto n5 = device();
    p::PolicyExecutor ex5(n5.io());
    auto noplay = context(&c);
    noplay.runtime.game_active = false;
    expect_blocked(ex5.execute(decision(p::Action::Mitigate, "gpu"), noplay), "no_active_game", n5);
    auto other = context(&c);
    other.session_id = "s-2";
    expect_blocked(ex5.execute(decision(p::Action::Mitigate, "gpu"), other), "stale_decision", n5);
    auto rec = context(&c);
    rec.runtime.recovery_failed = true;
    expect_blocked(ex5.execute(decision(p::Action::Mitigate, "gpu"), rec), "recovery_incomplete", n5);
    auto bad = context(&c);
    bad.runtime.transaction = TxState::Failed;
    expect_blocked(ex5.execute(decision(p::Action::Mitigate, "gpu"), bad), "invalid_transaction_state", n5);
}

void test_failures() {
    auto c = cpu_caps();
    { // 19. apply failure surfaced, engine rolled back
        auto n = device();
        n.reject.insert(MAX4);
        std::vector<std::string> journal;
        p::PolicyExecutor ex(n.io(), [&](const std::string &j) {
            journal.push_back(j);
            return true;
        });
        auto r = ex.execute(decision(p::Action::Mitigate, "cpu"), context(&c));
        CHECK(r.final_status == p::ExecutionStatus::ApplyFailed);
        CHECK(r.executed && !r.verified && r.rolled_back && r.restored); // partial execution not hidden
        CHECK(n.nodes[MAX0] == "1804800" && n.nodes[MAX4] == "2016000");
        CHECK(!ex.active());
        CHECK(rt::journal::parse(journal.back()).entries.empty()); // 21. existing rollback semantics
    }
    { // 20. verification failure surfaced, rolled back
        auto n = device();
        n.sticky.insert(MAX0);
        p::PolicyExecutor ex(n.io());
        auto r = ex.execute(decision(p::Action::Mitigate, "cpu"), context(&c));
        CHECK(r.final_status == p::ExecutionStatus::VerifyFailed && r.executed && !r.verified && r.rolled_back);
        CHECK(r.restored && n.nodes[MAX0] == "1804800");
    }
    { // 22. restore failure stays visible; journal kept
        auto n = device();
        std::vector<std::string> journal;
        p::PolicyExecutor ex(n.io(), [&](const std::string &j) {
            journal.push_back(j);
            return true;
        });
        CHECK(ex.execute(decision(p::Action::Mitigate, "cpu"), context(&c)).final_status == p::ExecutionStatus::Applied);
        n.reject_all = true;
        auto r = ex.execute(decision(p::Action::Restore, "transaction", "pd-r"), context(&c));
        CHECK(r.final_status == p::ExecutionStatus::RestoreFailed && !r.restored);
        CHECK(!rt::journal::parse(journal.back()).entries.empty());
        // Nothing to restore without an executor transaction: GameRuntime / recovery own theirs.
        auto n2 = device();
        p::PolicyExecutor ex2(n2.io());
        auto none = ex2.execute(decision(p::Action::Restore, "transaction"), context(&c));
        CHECK(none.final_status == p::ExecutionStatus::NothingToRestore && !none.executed && n2.written.empty());
    }
}

void test_idempotency_observatory_determinism() {
    auto c = cpu_caps();
    auto n = device();
    o::MemoryEventStore store(o::EventRegistry::builtin(), [] { return int64_t(1'760'000'000'000); });
    flux::bridge::ObservatoryBridge bridge(&store, [] { return int64_t(1'760'000'000'000); });
    bridge.session_context()("s-1");
    p::PolicyExecutor ex(n.io(), nullptr, bridge.transaction_observer());
    const auto d = decision(p::Action::Mitigate, "cpu");
    const auto before = p::explain(d);
    auto r = ex.execute(d, context(&c));
    const auto writes = n.written.size();
    // 23. same policy again: no duplicate execution; 32. no automatic escalation or re-application.
    auto again = ex.execute(decision(p::Action::Mitigate, "cpu", "pd-again"), context(&c));
    CHECK(again.final_status == p::ExecutionStatus::AlreadyActive && !again.executed);
    auto stronger = ex.execute(decision(p::Action::Boost, "cpu", "pd-boost"), context(&c));
    CHECK(stronger.final_status == p::ExecutionStatus::Blocked && blocked_by(stronger, "conflicting_active_transaction"));
    CHECK_EQ(n.written.size(), writes);
    // 26. Observatory: existing TRANSACTION_* events, valid, with the session id; 27. schema unchanged.
    o::EventQuery q;
    q.source = "transaction";
    auto events = store.query(q);
    std::set<std::string> types;
    for (auto &e : events) {
        types.insert(e.type);
        CHECK_EQ(e.session_id, std::string("s-1"));
        CHECK_EQ(e.transaction_id, r.transaction_id);
    }
    CHECK(types.count("TRANSACTION_BEGIN") && types.count("TRANSACTION_APPLY") && types.count("TRANSACTION_VERIFY"));
    ex.execute(decision(p::Action::Restore, "transaction", "pd-r"), context(&c));
    o::EventQuery restore_q;
    restore_q.type = "TRANSACTION_RESTORE";
    CHECK_EQ(store.query(restore_q).size(), size_t(1));
    CHECK_EQ(o::kSchemaVersion, 1);
    CHECK_EQ(o::EventRegistry::builtin().types().size(), size_t(19));
    // 29. the decision is not mutated; 33. capability metadata untouched.
    CHECK_EQ(p::explain(d), before);
    CHECK(c.resolve("cpufreq.policy0.scaling_max_freq").fact->verified);
    CHECK_EQ(c.resolve("cpufreq.policy0.scaling_max_freq").fact->value, std::string("v"));
    // 28. deterministic: identical inputs and state give identical results.
    auto run = [&] {
        auto nn = device();
        p::PolicyExecutor e(nn.io());
        return p::to_text(e.execute(decision(p::Action::Mitigate, "cpu"), context(&c)));
    };
    CHECK_EQ(run(), run());
    // 17. operations are only the trusted cpufreq ceilings (named in the result).
    for (auto &a : r.affected_capabilities) CHECK_EQ(a.operation, std::string("cpufreq_scaling_max_freq"));
}

} // namespace

int main() {
    test_no_action_observe();
    test_mitigate_and_boost();
    test_capability_gating();
    test_thermal_profile_and_integrity();
    test_failures();
    test_idempotency_observatory_determinism();
    return flux_test::report("policy_executor_test");
}
