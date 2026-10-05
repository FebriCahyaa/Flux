// Adaptive Optimization V1 (Phase 5): baseline, post-intervention evaluation, keep / rollback /
// observe, sustainability cost, hysteresis, cooldown, bottleneck relationship, thermal safety,
// determinism and execution-boundary isolation. Pure host tests: no device, no writes.
#include "flux_test.hpp"
#include "AdaptiveController.hpp"
#include "Event.hpp"

#include <fstream>
#include <sstream>

namespace a = zairenkai::adaptive;
namespace b = flux::bottleneck;
namespace o = flux::observatory;

namespace {

a::Sample s(int64_t t, std::optional<double> fps, a::Thermal th = a::Thermal::Boost, b::Kind k = b::Kind::Cpu,
            std::optional<double> cpu = 0.95, std::optional<double> gpu = 0.5) {
    a::Sample x;
    x.timestamp_ms = t;
    x.fps = fps;
    x.target_hz = 60;
    x.cpu_busy = cpu;
    x.gpu_busy = gpu;
    x.thermal = th;
    x.bottleneck = k;
    x.bottleneck_state = b::State::Confirmed;
    return x;
}

std::vector<a::Sample> win(int n, std::optional<double> fps, a::Thermal th = a::Thermal::Boost, b::Kind k = b::Kind::Cpu,
                           std::optional<double> cpu = 0.95) {
    std::vector<a::Sample> w;
    for (int i = 0; i < n; ++i) w.push_back(s(1000 + 2000 * i, fps, th, k, cpu));
    return w;
}

a::Intervention boost(const std::string &target = "cpu") { return {"BOOST:" + target, "pd-1", target, false, 0}; }

std::string source(const std::string &rel) {
    std::ifstream in(std::string(FLUX_SOURCE_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 1, 2, 16, 17, 18. baseline and evidence availability; UNKNOWN stays UNKNOWN.
void test_baseline_and_unknowns() {
    a::AdaptiveController c;
    c.reset("s-1");
    c.observe(s(1000, 41));
    c.observe(s(3000, std::nullopt)); // missing FPS: not zero
    CHECK(!c.admit("BOOST:cpu", "sig", 5000).allowed);
    CHECK(c.admit("BOOST:cpu", "sig", 5000).reason.rfind("insufficient_baseline", 0) == 0);
    c.observe(s(5000, 42));
    c.observe(s(7000, 40));
    CHECK(c.admit("BOOST:cpu", "sig", 7000).allowed);
    const auto sum = a::summarize({c.baseline().begin(), c.baseline().end()});
    CHECK_EQ(sum.samples, size_t(4));
    CHECK_EQ(sum.fps_samples, size_t(3));
    CHECK(sum.fps && *sum.fps > 40.9 && *sum.fps < 41.1);
    CHECK(!a::summarize({s(0, std::nullopt, a::Thermal::Unknown, b::Kind::Cpu, std::nullopt)}).cpu_busy.has_value());
    CHECK(!a::summarize({}).fps.has_value());
    // Stale / unverified Synrei is Unknown, never re-derived from temperatures.
    flux::thermal::ThermalSnapshot t;
    t.readable = true;
    t.verified = false;
    t.state = "safety";
    t.cpu.celsius = 95; // hot, but temperature alone never means safety
    CHECK(a::from_synrei(t) == a::Thermal::Unknown);
    t.verified = true;
    CHECK(a::from_synrei(t) == a::Thermal::Safety);
    CHECK(a::from_synrei(std::nullopt) == a::Thermal::Unknown);
    // 16. missing FPS after the intervention: observe, then roll back at the window limit.
    auto e = a::evaluate(win(5, 41), win(3, std::nullopt), "cpu", false);
    CHECK(e.verdict == a::Verdict::Observe);
    e = a::evaluate(win(5, 41), win(a::kMaxPostSamples, std::nullopt), "cpu", false);
    CHECK(e.verdict == a::Verdict::Rollback && e.reason == "no_fps_evidence");
    // 17, 18. thermal evidence lost: awaiting, then rollback; never kept on unknown sustainability.
    e = a::evaluate(win(5, 41), win(3, 58, a::Thermal::Unknown), "cpu", false);
    CHECK(e.verdict == a::Verdict::Observe && e.reason == "awaiting_thermal_evidence");
    e = a::evaluate(win(5, 41), win(a::kMaxPostSamples, 58, a::Thermal::Unknown), "cpu", false);
    CHECK(e.verdict == a::Verdict::Rollback && e.reason == "thermal_evidence_lost");
}

// 3, 4, 5, 6, 7, 8, 9, 14, 15. success definition, cost, keep / rollback / observe.
void test_evaluation_rules() {
    auto e = a::evaluate(win(5, 41), win(2, 58), "cpu", false); // 9. single/too few samples
    CHECK(e.verdict == a::Verdict::Observe && e.reason == "insufficient_post_evidence");
    e = a::evaluate(win(5, 41), win(3, 52), "cpu", false); // 5, 8. beneficial -> keep
    CHECK(e.verdict == a::Verdict::Keep && e.reason == "benefit");
    e = a::evaluate(win(5, 41), win(3, 41), "cpu", false); // 4. ineffective: not yet
    CHECK(e.verdict == a::Verdict::Observe && e.reason == "benefit_not_yet_shown");
    e = a::evaluate(win(5, 41), win(a::kMaxPostSamples, 41.5), "cpu", false); // 7. no benefit -> rollback
    CHECK(e.verdict == a::Verdict::Rollback && e.reason == "no_benefit");
    // 6. thermal degradation: Synrei safety after a performance intervention is an unacceptable cost,
    // even when FPS improved.
    auto post = win(3, 58);
    post[1].thermal = a::Thermal::Safety;
    e = a::evaluate(win(5, 41), post, "cpu", false);
    CHECK(e.verdict == a::Verdict::Rollback && e.reason == "thermal_safety_after_intervention");
    // Higher load on the target without benefit is a cost (14: cpu, 15: gpu).
    e = a::evaluate(win(5, 41, a::Thermal::Boost, b::Kind::Cpu, 0.70), win(3, 41, a::Thermal::Boost, b::Kind::Cpu, 0.90),
                    "cpu", false);
    CHECK(e.verdict == a::Verdict::Rollback && e.reason == "cost_without_benefit");
    std::vector<a::Sample> gb, gp;
    for (int i = 0; i < 5; ++i) gb.push_back(s(i, 41, a::Thermal::Boost, b::Kind::Gpu, 0.9, 0.70));
    for (int i = 0; i < 3; ++i) gp.push_back(s(i, 41, a::Thermal::Boost, b::Kind::Gpu, 0.9, 0.95));
    e = a::evaluate(gb, gp, "gpu", false);
    CHECK(e.verdict == a::Verdict::Rollback && e.reason == "cost_without_benefit");
    for (auto &x : gp) x.fps = 55;
    CHECK(a::evaluate(gb, gp, "gpu", false).verdict == a::Verdict::Keep); // 15. GPU benefit
    // A clock or utilisation change alone is never success.
    auto clocks = win(3, 41);
    for (auto &x : clocks) x.cpu_freq_ratio = 1.0;
    CHECK(a::evaluate(win(5, 41), clocks, "cpu", false).verdict != a::Verdict::Keep);
    // One high FPS sample is not success.
    auto spike = win(3, 41);
    spike[0].fps = 60;
    CHECK(a::evaluate(win(5, 41), spike, "cpu", false).verdict == a::Verdict::Observe);
    // MITIGATE is judged on thermal relief, never rolled back for performance.
    CHECK(a::evaluate(win(5, 41), win(3, 30, a::Thermal::Safety), "cpu", true).verdict == a::Verdict::Keep);
}

// 13, 19. bottleneck relationship.
void test_bottleneck_relationship() {
    // GPU intervention, then the CPU becomes the confirmed limit: judged on frame delivery.
    std::vector<a::Sample> post;
    for (int i = 0; i < 3; ++i) post.push_back(s(i, 50, a::Thermal::Boost, b::Kind::Cpu));
    auto e = a::evaluate(win(5, 41, a::Thermal::Boost, b::Kind::Gpu), post, "gpu", false);
    CHECK(e.bottleneck_shifted);
    CHECK(e.verdict == a::Verdict::Keep && e.reason == "benefit_bottleneck_shifted");
    // Shifted but no benefit: not a failure verdict before the window is full.
    for (auto &x : post) x.fps = 41;
    CHECK(a::evaluate(win(5, 41, a::Thermal::Boost, b::Kind::Gpu), post, "gpu", false).verdict == a::Verdict::Observe);
    // Conflicting bottleneck evidence is not read as a shift.
    for (auto &x : post) x.bottleneck_conflict = true;
    CHECK(!a::evaluate(win(5, 41, a::Thermal::Boost, b::Kind::Gpu), post, "gpu", false).bottleneck_shifted);
}

void warm(a::AdaptiveController &c, int64_t t0, double fps = 41) {
    for (int i = 0; i < 3; ++i) c.observe(s(t0 + 2000 * i, fps));
}

// 10, 11, 12, 21, 22, 23. cooldown, hysteresis, no escalation, failures.
void test_lifecycle() {
    a::AdaptiveController c;
    c.reset("s-1");
    warm(c, 1000);
    CHECK(c.admit("BOOST:cpu", "sigA", 6000).allowed);
    c.executed(boost(), a::ExecutionOutcome::Applied, "sigA", 6000);
    CHECK(c.phase() == a::Phase::Evaluating);
    CHECK(!c.admit("BOOST:cpu", "sigA", 6000).allowed); // 12. no escalation while evaluating
    for (int i = 0; i < 3; ++i) c.observe(s(8000 + 2000 * i, 55));
    auto e = c.evaluate(12000);
    CHECK(e && e->verdict == a::Verdict::Keep);
    CHECK(c.phase() == a::Phase::Kept);
    CHECK(c.cooldown_until() == 12000 + a::kCooldownAfterKeepMs);
    CHECK(c.admit("BOOST:cpu", "sigA", 999999).reason.rfind("active", 0) == 0); // kept: no stronger step

    // Ineffective -> rollback -> cooldown; identical evidence blocked after cooldown (hysteresis).
    a::AdaptiveController r;
    r.reset("s-2");
    warm(r, 1000);
    r.executed(boost(), a::ExecutionOutcome::Applied, "sigA", 6000);
    for (int i = 0; i < int(a::kMaxPostSamples); ++i) r.observe(s(8000 + 2000 * i, 41));
    e = r.evaluate(30000);
    CHECK(e && e->verdict == a::Verdict::Rollback);
    r.restored(true, 30000);
    CHECK(r.phase() == a::Phase::Cooldown);
    CHECK(r.admit("BOOST:cpu", "sigA", 30001).reason.rfind("cooldown", 0) == 0); // 10.
    warm(r, 31000);
    const int64_t after = 30000 + a::kCooldownAfterRollbackMs + 1;
    CHECK(r.admit("BOOST:cpu", "sigA", after).reason.rfind("hysteresis", 0) == 0); // 11. same evidence
    CHECK(r.admit("BOOST:cpu", "sigB", after).allowed);                              // changed evidence
    // Second rollback of the same key: held for the session (no BOOST/rollback oscillation).
    r.executed(boost(), a::ExecutionOutcome::Applied, "sigB", after);
    for (int i = 0; i < int(a::kMaxPostSamples); ++i) r.observe(s(after + 2000 * i, 41));
    r.evaluate(after + 30000);
    r.restored(true, after + 30000);
    CHECK(r.phase() == a::Phase::Held);
    CHECK(!r.admit("BOOST:cpu", "sigC", after + 10000000).allowed);

    // 21, 22. failed / partially applied (engine rolled back) execution: cooldown, same evidence blocked.
    a::AdaptiveController f;
    f.reset("s-3");
    warm(f, 1000);
    f.executed(boost(), a::ExecutionOutcome::Failed, "sigA", 6000);
    CHECK(f.phase() == a::Phase::Cooldown);
    CHECK(!f.active().has_value());
    CHECK(f.admit("BOOST:cpu", "sigA", 6000 + a::kCooldownAfterFailureMs + 1).reason.rfind("hysteresis", 0) == 0);
    // 23. restore failure: no further interventions this session.
    a::AdaptiveController x;
    x.reset("s-4");
    warm(x, 1000);
    x.executed(boost(), a::ExecutionOutcome::Applied, "sigA", 6000);
    x.restored(false, 7000);
    CHECK(x.restore_failed());
    CHECK(x.admit("BOOST:cpu", "sigZ", 99999999).reason.rfind("restore_failed", 0) == 0);
    // Unchanged (blocked / no change / already active) is not an intervention.
    a::AdaptiveController u;
    u.reset("s-5");
    warm(u, 1000);
    u.executed(boost(), a::ExecutionOutcome::Unchanged, "sigA", 6000);
    CHECK(u.phase() == a::Phase::Idle && u.admit("BOOST:cpu", "sigA", 6000).allowed);
    // Session reset clears everything.
    r.reset("s-6");
    CHECK(r.phase() == a::Phase::Idle && r.baseline().empty());
}

// 24. determinism: same evidence sequence, same verdicts.
void test_determinism() {
    auto run = [] {
        a::AdaptiveController c;
        c.reset("s");
        warm(c, 1000);
        c.executed(boost(), a::ExecutionOutcome::Applied, "sig", 6000);
        std::string out;
        for (int i = 0; i < 12; ++i) {
            c.observe(s(8000 + 2000 * i, 41 + (i % 3)));
            if (auto e = c.evaluate(8000 + 2000 * i)) out += std::string(a::to_string(e->verdict)) + ":" + e->reason + ";";
            if (c.phase() != a::Phase::Evaluating) break;
        }
        return out + a::to_string(c.phase());
    };
    CHECK_EQ(run(), run());
}

// 25-33. execution boundary and isolation.
void test_isolation() {
    for (auto f : {"jni/adaptive/AdaptiveController.hpp", "jni/adaptive/AdaptiveController.cpp",
                   "jni/adaptive/InterventionEvaluation.hpp", "jni/adaptive/InterventionEvaluation.cpp"}) {
        const auto src = source(f);
        CHECK(!src.empty());
        for (auto bad : {"\"/sys", "\"/proc", "\"/dev", "/data/adb", "Io ", "io.write", ".write(", "flux::runtime",
                         "RuntimePlan", "NodeWriteOperation", "flux::policy", "#include \"PolicyExecutor", "#include \"DecisionEngine", "#include \"Transaction", "std::thread", "pthread",
                         "rand(", "random", "chrono", "time(", "ofstream", "fopen", "system(", "popen", "RuntimeMetricsCollector",
                         "SynreiThermalAdapter", "FpsObservationSlot", "BottleneckModel(", "EventStore", "TeeEventSink",
                         "scaling_max_freq", "max_gpuclk", "governor"})
            CHECK(src.find(bad) == std::string::npos);
    }
    // 28, 29. the live loop still executes and rolls back only through PolicyExecutor.
    const auto live = source("jni/policy/LivePolicyController.cpp");
    CHECK(live.find("executor_.execute(") != std::string::npos);
    CHECK(live.find("NodeWriteOperation") == std::string::npos && live.find("Transaction(") == std::string::npos);
    // 20. capability gating untouched: the executor still refuses unverified controls.
    CHECK(source("jni/policy/PolicyExecutor.cpp").find("capability_unverified") != std::string::npos);
    // 30, 31. GameRuntime and Synrei code paths not referenced by the adaptive module.
    for (auto f : {"jni/adaptive/AdaptiveController.cpp", "jni/adaptive/InterventionEvaluation.cpp"}) {
        const auto src = source(f);
        CHECK(src.find("GamePerformanceRuntime") == std::string::npos && src.find("hicod") == std::string::npos &&
              src.find("hico/state") == std::string::npos);
    }
    // 32, 33. no new event type, schema v1 (B-35 scope preserved).
    CHECK_EQ(o::kSchemaVersion, 1);
    const auto reg = o::EventRegistry::builtin();
    CHECK_EQ(reg.types().size(), size_t(19));
}

} // namespace

int main() {
    test_baseline_and_unknowns();
    test_evaluation_rules();
    test_bottleneck_relationship();
    test_lifecycle();
    test_determinism();
    test_isolation();
    return flux_test::report("adaptive_test");
}
