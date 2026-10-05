// Live decision integration (Phase 4C): fresh in-session evidence -> DecisionEngine ->
// PolicyExecutor on the session tick; lifecycle gating; restore before GameRuntime restore;
// failure isolation; final BOTTLENECK_ASSESSED untouched; no new mechanisms.
#include "flux_test.hpp"
#include "Event.hpp"
#include "LivePolicyController.hpp"

#include <fstream>
#include <set>
#include <sstream>

namespace b = flux::bottleneck;
namespace ctx = flux::context;
namespace k = flux::kernel;
namespace m = flux::metrics;
namespace o = flux::observatory;
namespace p = flux::policy;
namespace rt = flux::runtime;
namespace s = flux::session;
namespace t = flux::thermal;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
    Kind kind(const std::string &q) const override {
        if (files.count(q)) return Kind::File;
        auto it = files.lower_bound(q + "/");
        return it != files.end() && it->first.rfind(q + "/", 0) == 0 ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &q) const override {
        if (!files.count(q)) return std::nullopt;
        return files.at(q);
    }
    std::vector<std::string> list(const std::string &d) const override {
        std::set<std::string> out;
        for (auto &[q, _] : files)
            if (q.rfind(d + "/", 0) == 0) {
                auto rest = q.substr(d.size() + 1);
                out.insert(rest.substr(0, rest.find('/')));
            }
        return {out.begin(), out.end()};
    }
    bool writable_hint(const std::string &) const override { return false; }
};

/// Shared ordering log: executor writes and participant ends land here in the order they happen.
std::vector<std::string> order;

struct FakeNodes {
    std::map<std::string, std::string> nodes;
    std::set<std::string> sticky;
    bool reject_all = false;
    int writes = 0;
    rt::Io io() {
        rt::Io io;
        io.exists = [this](const std::string &q) { return nodes.count(q) > 0; };
        io.read = [this](const std::string &q) -> std::optional<std::string> {
            auto it = nodes.find(q);
            if (it == nodes.end()) return std::nullopt;
            return it->second + "\n";
        };
        io.write = [this](const std::string &q, const std::string &v) {
            ++writes;
            order.push_back("write " + q + "=" + v);
            if (!nodes.count(q) || reject_all) return false;
            if (!sticky.count(q)) nodes[q] = v;
            return true;
        };
        return io;
    }
};

const std::string P4 = "sys/devices/system/cpu/cpufreq/policy4/";
const std::string MAX4 = "/" + P4 + "scaling_max_freq";

FakeNodes nodes() {
    FakeNodes f;
    f.nodes[MAX4] = "2016000";
    f.nodes["/" + P4 + "scaling_min_freq"] = "710400";
    f.nodes["/" + P4 + "cpuinfo_max_freq"] = "2419200";
    f.nodes["/" + P4 + "scaling_available_frequencies"] = "710400 1574400 2016000 2419200";
    return f;
}

ctx::CapabilityFact fact(const std::string &id, const std::string &domain, const std::string &interface,
                         const std::string &value, bool verified) {
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
    f.value = value;
    return f;
}

ctx::CapabilityContext caps(bool verified = true) {
    ctx::CapabilityContext c;
    c.publish("kernel", {fact("cpufreq.policy4.scaling_max_freq", "cpufreq", P4 + "scaling_max_freq", "2016000", verified),
                         fact("display.refresh.current_hz", "display", "", "60", false)});
    return c;
}

/// Live evidence feeding the EXISTING sampler seams (FPS slot, Synrei source, analyzer).
struct Evidence {
    std::string synrei = "boost";
    bool synrei_verified = true;
    std::optional<double> cpu_temp; // temperature reading only; never a thermal verdict
    double fps = 41;
    b::Kind kind = b::Kind::Cpu;
    b::State state = b::State::Confirmed;
    int64_t now = 0;
    int analyzer_calls = 0;
};

t::ThermalSnapshot snapshot(const Evidence &e, int64_t now) {
    t::ThermalSnapshot s;
    s.timestamp_ms = now;
    s.source = "synrei:/dev/hico/state";
    s.state = e.synrei;
    s.readable = true;
    s.verified = e.synrei_verified;
    s.confidence = e.synrei_verified ? ctx::Confidence::High : ctx::Confidence::None;
    if (e.synrei_verified && e.synrei == "safety") s.constraint = t::Constraint::Constrained;
    else if (e.synrei_verified && e.synrei == "boost") s.constraint = t::Constraint::Unconstrained;
    if (!e.synrei_verified) s.note = "stale";
    if (e.cpu_temp) {
        s.cpu.celsius = e.cpu_temp;
        s.cpu.readable = true;
    }
    return s;
}

b::Assessment assessment(const Evidence &e, int64_t now, size_t n) {
    b::Assessment a;
    a.timestamp_ms = now;
    a.samples = n;
    b::Observation ob;
    ob.kind = e.kind;
    ob.state = e.state;
    ob.confidence = ctx::Confidence::High;
    ob.source = "session";
    ob.timestamp_ms = now;
    ob.evidence = {{"busy", "peak 0.97", ">= 0.90", "session", now}};
    a.observations = {ob};
    a.primary = e.kind;
    a.primary_state = e.state;
    return a;
}

/// Stand-in for GameRuntime: records its end so ordering against the executor restore is visible.
struct FakeGameRuntime : s::SessionParticipant {
    const char *name() const override { return "game_runtime"; }
    void begin(const s::SessionInfo &) override { order.push_back("game_runtime begin"); }
    bool end(const s::SessionInfo &, s::EndReason) override {
        order.push_back("game_runtime restore");
        return true;
    }
};

struct Rig {
    FakeFs fs;
    Evidence ev;
    ctx::CapabilityContext c;
    FakeNodes dev = nodes();
    m::RuntimeMetricsSampler sampler;
    p::PolicyExecutor executor;
    bool recovery_clean = true;
    bool game_active = true;
    rt::TxState game_tx = rt::TxState::Active;
    p::ProfileMode profile = p::ProfileMode::Performance;
    p::LivePolicyController live;
    m::SamplerParticipant sampler_part;
    p::LivePolicyParticipant live_part;
    FakeGameRuntime game;
    s::SessionManager mgr;
    int final_results = 0;
    std::vector<s::SessionNotice> notices;

    explicit Rig(bool verified = true)
        : c(caps(verified)),
          sampler(fs, &c, {2000, 10, 3000},
                  [this]() -> std::optional<m::FpsObservation> {
                      m::FpsObservation f;
                      f.timestamp_ms = ev.now;
                      f.fps = ev.fps;
                      f.valid = true;
                      f.source = "test";
                      return f;
                  },
                  [this](int64_t now) { return snapshot(ev, now); }),
          executor(dev.io()),
          live(sampler, &c, executor, [this] { return p::ProfileIntent{profile}; },
               [this] {
                   p::RuntimeEvidence r;
                   r.game_active = game_active;
                   r.transaction = game_tx;
                   return r;
               }),
          sampler_part(sampler), live_part(live, [this] { return recovery_clean; }) {
        fs.files["proc/stat"] = "cpu  100 0 0 900 0 0 0 0\ncpu0 100 0 0 900 0 0 0 0\n";
        fs.files["proc/meminfo"] = "MemTotal: 4000000 kB\nMemAvailable: 1000000 kB\n";
        sampler.set_analyzer([this](const b::BottleneckInputs &in) {
            ++ev.analyzer_calls;
            return assessment(ev, in.now_ms, in.session.samples.size());
        });
        sampler.set_result_sink([this](const b::BottleneckResult &) { ++final_results; }, nullptr);
        mgr.add(&game);          // GameRuntime first: begins first, ends last
        mgr.add(&sampler_part);  // RuntimeMetrics
        mgr.add(&live_part);     // live policy last: ends FIRST
        mgr.set_observer([this](const s::SessionNotice &n) { notices.push_back(n); });
        order.clear();
    }

    /// Advances the session clock until the sampler produced `n` more samples.
    void samples(int n) {
        const auto want = sampler.samples_taken() + n;
        for (int i = 0; i < 50 && sampler.samples_taken() < want; ++i) {
            ev.now += 1000;
            mgr.tick(ev.now);
        }
    }
    bool start() {
        mgr.recover();
        ev.now = 1000;
        s::SessionKey key;
        key.package = "com.game";
        key.pid = 42;
        return mgr.begin(key, ev.now);
    }
};

std::string source(const std::string &rel) {
    std::ifstream in(std::string(FLUX_SOURCE_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 1, 2, 3, 4, 5, 6: decisions taken from live evidence follow the B-38 semantics.
void test_live_b38_semantics() {
    for (auto kind : {b::Kind::Cpu, b::Kind::Gpu}) {
        Rig r;
        r.ev.kind = kind;
        r.profile = p::ProfileMode::Balance; // BOOST requirements unmet
        CHECK(r.start());
        r.samples(1);
        CHECK(r.live.last().has_value());
        CHECK(r.live.last()->decision.action == p::Action::Observe); // never MITIGATE under boost
        CHECK_EQ(r.dev.writes, 0);
    }
    {
        Rig r; // 5. full BOOST requirements
        CHECK(r.start());
        r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
        CHECK(r.live.last()->decision.action == p::Action::Boost);
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::Applied);
        CHECK_EQ(r.dev.nodes[MAX4], std::string("2419200"));
    }
    {
        Rig r; // 6. missing FPS -> no BOOST
        r.ev.fps = 60;
        CHECK(r.start());
        r.samples(1);
        CHECK(r.live.last()->decision.action != p::Action::Boost);
        CHECK_EQ(r.dev.writes, 0);
    }
    {
        Rig r; // 3. safety + verified control -> MITIGATE
        r.ev.synrei = "safety";
        CHECK(r.start());
        r.samples(1);
        CHECK(r.live.last()->decision.action == p::Action::Mitigate);
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::Applied);
        CHECK_EQ(r.dev.nodes[MAX4], std::string("1574400")); // one step below the current ceiling
    }
    {
        Rig r(false); // 4. safety + unverified control -> OBSERVE
        r.ev.synrei = "safety";
        CHECK(r.start());
        r.samples(1);
        CHECK(r.live.last()->decision.action == p::Action::Observe);
        CHECK_EQ(r.dev.writes, 0);
    }
}

// 7. stale / unknown thermal is passed as recorded and never reinterpreted.
void test_stale_thermal() {
    Rig r;
    r.ev.synrei_verified = false;
    CHECK(r.start());
    r.samples(1);
    const auto &d = r.live.last()->decision;
    CHECK(d.action == p::Action::Observe);
    bool unknown = false;
    for (auto &c : d.constraints) unknown = unknown || c.kind == p::ConstraintKind::ThermalUnknown;
    CHECK(unknown);
    CHECK_EQ(r.dev.writes, 0);
}

// 8. fresh evidence only; 10. no repeated execution; 9. final result untouched; 24. determinism.
void test_fresh_evidence_and_repeats() {
    Rig r;
    CHECK(r.start());
    CHECK_EQ(r.live.evaluations(), uint64_t(0));
    r.mgr.tick(r.ev.now); // same instant: no new sample
    CHECK_EQ(r.live.evaluations(), uint64_t(0));
    r.samples(1);
    CHECK_EQ(r.live.evaluations(), uint64_t(1));
    CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::NotExecuted); // no baseline yet
    CHECK(r.live.last()->adaptive_gate.rfind("insufficient_baseline", 0) == 0);
    CHECK_EQ(r.live.last()->sample_index, r.sampler.samples_taken());
    r.live.tick(r.mgr.current().id, r.ev.now); // same evidence again
    CHECK_EQ(r.live.evaluations(), uint64_t(1));
    CHECK_EQ(r.live.skipped(), std::string("no_fresh_evidence"));
    r.samples(2); // baseline complete: BOOST admitted and applied
    CHECK_EQ(r.live.evaluations(), uint64_t(3));
    CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::Applied);
    const int writes = r.dev.writes;
    r.samples(1); // new sample, same decision: under evaluation, not executed again (no escalation)
    CHECK_EQ(r.live.evaluations(), uint64_t(4));
    CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::NotExecuted);
    CHECK(r.live.last()->adaptive_gate.rfind("evaluating", 0) == 0);
    CHECK_EQ(r.dev.writes, writes);
    // 9. live assessment never produced or replaced the session-final result.
    CHECK(!r.sampler.final_assessment().has_value());
    CHECK_EQ(r.final_results, 0);
    r.mgr.end(s::EndReason::Exit, r.ev.now + 1);
    CHECK_EQ(r.final_results, 1);
    // 24. identical evidence -> identical decision.
    auto a = r.live.inputs("s-x", 5000), b2 = r.live.inputs("s-x", 5000);
    p::DecisionEngine e;
    CHECK_EQ(p::explain(e.evaluate(a)), p::explain(e.evaluate(b2)));
}

// 11. active session only; 12. wrong session blocked; 13. incomplete recovery blocks.
void test_gating() {
    {
        Rig r;
        CHECK(!r.live.tick("s-none", 100).has_value());
        CHECK_EQ(r.live.skipped(), std::string("disabled"));
        CHECK(r.start());
        r.samples(1);
        const auto evals = r.live.evaluations();
        r.ev.now += 2000;
        r.sampler.tick(r.ev.now); // fresh sample, but asked for another session
        CHECK(!r.live.tick("s-other", r.ev.now).has_value());
        CHECK_EQ(r.live.skipped(), std::string("session_mismatch"));
        CHECK_EQ(r.live.evaluations(), evals);
        r.mgr.end(s::EndReason::Exit, r.ev.now);
        CHECK(!r.live.enabled());
        CHECK(!r.live.tick(r.mgr.current().id, r.ev.now + 5000).has_value());
    }
    {
        Rig r;
        r.recovery_clean = false;
        CHECK(r.start());
        r.samples(2);
        CHECK_EQ(r.live.evaluations(), uint64_t(0));
        CHECK_EQ(r.live.skipped(), std::string("recovery_incomplete"));
        CHECK_EQ(r.dev.writes, 0);
    }
    {
        Rig r;
        r.game_tx = rt::TxState::Restoring; // conflicting GameRuntime transaction
        CHECK(r.start());
        r.samples(1);
        CHECK_EQ(r.live.evaluations(), uint64_t(0));
        CHECK(r.live.skipped().find("conflicting_transaction") == 0);
    }
}

// 14. policy restore before GameRuntime restore; every end reason.
void test_restore_order() {
    for (auto why : {s::EndReason::Exit, s::EndReason::FocusLost, s::EndReason::ProcessDeath, s::EndReason::Failure,
                     s::EndReason::DaemonStop}) {
        Rig r;
        CHECK(r.start());
        r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
        CHECK(r.executor.active());
        order.clear();
        r.mgr.end(why, r.ev.now + 1);
        CHECK(!r.executor.active());
        CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));
        CHECK(order.size() >= 2);
        CHECK_EQ(order.front(), "write " + MAX4 + "=2016000");
        CHECK_EQ(order.back(), std::string("game_runtime restore"));
        CHECK(r.live.last()->decision.action == p::Action::Restore);
        CHECK(r.notices.back().clean);
    }
    {
        Rig r; // switch: the old session's changes are restored before the new one begins
        CHECK(r.start());
        r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
        order.clear();
        s::SessionKey other;
        other.package = "com.other";
        other.pid = 77;
        r.mgr.begin(other, r.ev.now + 1);
        CHECK_EQ(order.front(), "write " + MAX4 + "=2016000");
        CHECK(!r.executor.active());
    }
}

// 15. executor failure does not break GameRuntime; 16. failed apply rolls back; 17. restore failure visible.
void test_failures() {
    {
        Rig r;
        r.dev.reject_all = true;
        CHECK(r.start());
        r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::ApplyFailed);
        CHECK(!r.executor.active());
        r.live.set_observer([](const p::LiveEvaluation &) { throw std::runtime_error("observer"); });
        r.samples(1); // observer failure isolated
        CHECK(r.mgr.active());
        order.clear();
        r.mgr.end(s::EndReason::Exit, r.ev.now + 1);
        CHECK_EQ(order.back(), std::string("game_runtime restore"));
        CHECK(r.notices.back().clean);
    }
    {
        Rig r;
        r.dev.sticky.insert(MAX4); // read-back differs
        CHECK(r.start());
        r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::VerifyFailed);
        CHECK(r.live.last()->execution.rolled_back);
        CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));
    }
    {
        Rig r;
        CHECK(r.start());
        r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
        r.dev.reject_all = true;
        order.clear();
        r.mgr.end(s::EndReason::Exit, r.ev.now + 1);
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::RestoreFailed);
        CHECK(!r.notices.back().clean);                         // visible in the session notice
        CHECK_EQ(order.back(), std::string("game_runtime restore")); // GameRuntime still restored
    }
}

// 18-23, 26: no paths, no shell, no second framework / collectors, no mutation, schema unchanged.
void test_isolation() {
    const auto hpp = source("jni/policy/LivePolicyController.hpp");
    const auto cpp = source("jni/policy/LivePolicyController.cpp");
    CHECK(!cpp.empty() && !hpp.empty());
    for (const auto &src : {hpp, cpp}) {
        for (auto bad : {"\"/sys", "\"/proc", "\"/dev", "/data/adb", "system(", "popen", "execv", "execl", "fork(", "std::thread",
                         "pthread", "ofstream", "fopen", "RuntimeMetricsCollector", "SynreiThermalAdapter",
                         "FpsObservationSlot", "Transaction(", "journal::", "BottleneckModel(", "learn", "neural", "random",
                         "escalat"})
            CHECK(src.find(bad) == std::string::npos);
    }
    // 22. capability facts and the decision are not mutated by the live loop.
    Rig r;
    CHECK(r.start());
    r.samples(3); // Phase 5: BOOST waits for a 3-sample FPS baseline
    const auto before = p::explain(r.live.last()->decision);
    CHECK(r.c.resolve("cpufreq.policy4.scaling_max_freq").fact->verified);
    CHECK_EQ(r.c.resolve("cpufreq.policy4.scaling_max_freq").fact->value, std::string("2016000"));
    p::LiveEvaluation copy = *r.live.last();
    CHECK_EQ(p::explain(copy.decision), before);
    // 26. Observatory schema unchanged.
    CHECK_EQ(o::kSchemaVersion, 1);
    CHECK_EQ(o::EventRegistry::builtin().types().size(), size_t(19));
}

// Phase 5: adaptive outcome evaluation through the real executor / transaction path.
void test_adaptive_live() {
    { // beneficial BOOST is kept; the kernel value stays applied
        Rig r;
        CHECK(r.start());
        r.samples(3);
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::Applied);
        r.ev.fps = 56; // frame delivery improves after the intervention
        r.samples(3);
        CHECK(r.live.adaptive().phase() == zairenkai::adaptive::Phase::Kept);
        CHECK_EQ(r.dev.nodes[MAX4], std::string("2419200"));
        bool kept = false;
        if (r.live.last()->adaptive) kept = r.live.last()->adaptive->verdict == zairenkai::adaptive::Verdict::Keep;
        CHECK(kept || r.live.last()->adaptive_gate.rfind("active", 0) == 0);
    }
    { // Synrei safety after BOOST: rollback through PolicyExecutor RESTORE (read back at snapshot)
        Rig r;
        CHECK(r.start());
        r.samples(3);
        CHECK(r.executor.active());
        r.ev.synrei = "safety";
        r.samples(1);
        CHECK(r.live.last()->adaptive.has_value());
        CHECK(r.live.last()->adaptive->reason == "thermal_safety_after_intervention");
        CHECK(r.live.last()->decision.action == p::Action::Restore);
        CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::Restored);
        CHECK(!r.executor.active());
        CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));
    }
    { // ineffective BOOST rolled back; identical evidence stays blocked after the cooldown
        Rig r;
        CHECK(r.start());
        r.samples(3);
        const int applied_writes = r.dev.writes;
        r.samples(int(zairenkai::adaptive::kMaxPostSamples));
        CHECK(!r.executor.active());
        CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));
        CHECK(r.live.adaptive().phase() == zairenkai::adaptive::Phase::Cooldown);
        const int after_rollback = r.dev.writes;
        CHECK(after_rollback > applied_writes);
        r.samples(20); // 2 x 20 samples of 2 s: > 60 s, cooldown over, evidence unchanged
        r.samples(20);
        CHECK_EQ(r.dev.writes, after_rollback); // no BOOST -> rollback -> BOOST oscillation
        CHECK(r.live.last()->adaptive_gate.rfind("hysteresis", 0) == 0);
    }
    { // unverified capability never becomes an adaptive candidate
        Rig r(false);
        CHECK(r.start());
        r.samples(5);
        CHECK(r.live.last()->decision.action != p::Action::Boost);
        CHECK(r.live.adaptive().phase() == zairenkai::adaptive::Phase::Idle);
        CHECK_EQ(r.dev.writes, 0);
    }
}

// B-42: thermal safety with active interventions and the state-based thermal hold.
void test_b42_thermal_hold() {
    Rig r;
    CHECK(r.start());
    r.samples(3);
    CHECK(r.executor.active() && r.executor.active_key() == "BOOST:cpu");
    CHECK(!r.live.thermal_hold());

    // 3, 4. safety with BOOST active: BOOST restored, hold entered.
    r.ev.synrei = "safety";
    r.samples(1);
    CHECK(r.live.last()->execution.final_status == p::ExecutionStatus::Restored);
    CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));
    CHECK(r.live.thermal_hold());

    // 5, 6, 7. safety persists: hold stays, BOOST never re-applied. 14: MITIGATE waits for the cooldown.
    r.samples(5);
    CHECK(r.live.thermal_hold());
    CHECK(r.live.last()->decision.action == p::Action::Mitigate);
    CHECK(r.live.last()->adaptive_gate.rfind("cooldown", 0) == 0);
    CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));

    // 1. after the cooldown, safety with nothing active -> MITIGATE (one step down).
    r.samples(20);
    r.samples(10);
    CHECK(r.executor.active() && r.executor.active_key() == "MITIGATE:cpu");
    CHECK_EQ(r.dev.nodes[MAX4], std::string("1574400"));

    // 2, 13. safety with the matching MITIGATE active: NO_ACTION, no restore, no oscillation.
    const int writes = r.dev.writes;
    std::set<std::string> actions;
    for (int i = 0; i < 12; ++i) {
        r.samples(1);
        actions.insert(p::to_string(r.live.last()->decision.action));
    }
    CHECK(actions == std::set<std::string>{"NO_ACTION"});
    CHECK_EQ(r.dev.writes, writes);
    CHECK_EQ(r.dev.nodes[MAX4], std::string("1574400"));
    CHECK(r.live.thermal_hold());

    // 10, 11. unknown / stale Synrei does not clear the hold; 12. nor does a cool temperature alone.
    r.ev.synrei_verified = false;
    r.ev.synrei = "boost";
    r.samples(3);
    CHECK(r.live.thermal_hold());
    r.ev.synrei_verified = true;
    r.ev.synrei = "safety";
    r.ev.cpu_temp = 30;
    r.samples(2);
    CHECK(r.live.thermal_hold());

    // 8. a fresh verified state other than safety clears it; the clearing sample executes nothing.
    r.ev.synrei = "boost";
    r.samples(1);
    CHECK(!r.live.thermal_hold());
    // 9. no automatic re-application: the mitigation stays, no BOOST is applied on following samples.
    r.samples(6);
    CHECK(r.executor.active_key() == "MITIGATE:cpu");
    CHECK_EQ(r.dev.nodes[MAX4], std::string("1574400"));
    CHECK_EQ(r.dev.writes, writes);

    // Session end restores the mitigation before GameRuntime, as before.
    r.mgr.end(s::EndReason::Exit, r.ev.now + 1);
    CHECK_EQ(r.dev.nodes[MAX4], std::string("2016000"));
    CHECK(!r.live.thermal_hold() || !r.live.enabled());
}

} // namespace

int main() {
    test_live_b38_semantics();
    test_stale_thermal();
    test_fresh_evidence_and_repeats();
    test_gating();
    test_restore_order();
    test_failures();
    test_isolation();
    test_adaptive_live();
    test_b42_thermal_hold();
    return flux_test::report("live_policy_test");
}
