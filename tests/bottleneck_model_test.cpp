// Bottleneck observation (Step 8.6): CPU / GPU / thermal evidence, conflicting evidence,
// insufficient data, display-bound, and the evidence contract (evidence, confidence, source, time).
#include "flux_test.hpp"
#include "BottleneckModel.hpp"

#include <map>

namespace b = flux::bottleneck;
namespace ctx = flux::context;

namespace {

b::RuntimeSample sample(int64_t t, double fps, double target) {
    b::RuntimeSample s;
    s.timestamp_ms = t;
    s.fps = fps;
    s.target_hz = target;
    return s;
}

std::vector<b::RuntimeSample> series(int n, double fps, double target, void (*fill)(b::RuntimeSample &)) {
    std::vector<b::RuntimeSample> out;
    for (int i = 0; i < n; ++i) {
        auto s = sample(1000 + i * 1000, fps, target);
        if (fill) fill(s);
        out.push_back(s);
    }
    return out;
}

void cpu_bound(b::RuntimeSample &s) {
    s.cpu_busiest_core = 0.98;
    s.cpu_freq_ratio = 1.0;
    s.gpu_busy = 0.45;
}
void gpu_bound(b::RuntimeSample &s) {
    s.gpu_busy = 0.97;
    s.gpu_freq_ratio = 1.0;
    s.cpu_busiest_core = 0.5;
}
void both_bound(b::RuntimeSample &s) {
    s.cpu_busiest_core = 0.97;
    s.cpu_freq_ratio = 1.0;
    s.gpu_busy = 0.97;
}

b::BottleneckInputs inputs(std::vector<b::RuntimeSample> samples) {
    b::BottleneckInputs in;
    in.session = {"com.example.game", std::move(samples)};
    in.performance = {"performance", false, 120};
    in.now_ms = 99000;
    return in;
}

void check_contract(const b::Assessment &a) {
    CHECK_EQ(a.observations.size(), size_t(6));
    for (const auto &o : a.observations) {
        CHECK(o.kind != b::Kind::Unknown);
        if (o.state == b::State::Unknown) {
            CHECK(o.confidence == ctx::Confidence::None);
            CHECK(!o.note.empty());
            continue;
        }
        CHECK(!o.evidence.empty()); // no conclusion without evidence
        CHECK(!o.source.empty());
        CHECK(o.timestamp_ms > 0);
        for (const auto &e : o.evidence) {
            CHECK(!e.metric.empty());
            CHECK(!e.source.empty());
            CHECK(e.timestamp_ms > 0);
        }
    }
}

void test_cpu_evidence() {
    auto a = b::assess(inputs(series(6, 40, 60, cpu_bound)));
    check_contract(a);
    auto *cpu = a.get(b::Kind::Cpu);
    CHECK(cpu && cpu->state == b::State::Confirmed && cpu->confidence == ctx::Confidence::High);
    CHECK(a.primary == b::Kind::Cpu && a.primary_state == b::State::Confirmed && !a.conflict);
    CHECK(a.get(b::Kind::Gpu)->state <= b::State::Possible);
    // Saturated but meeting the target: only POSSIBLE (nothing is being held back).
    auto met = b::assess(inputs(series(6, 60, 60, cpu_bound)));
    CHECK(met.get(b::Kind::Cpu)->state == b::State::Possible);
    CHECK(met.primary == b::Kind::Unknown);
    // Without the GPU measured, CPU cannot be CONFIRMED (no exclusion), only LIKELY.
    auto nogpu = b::assess(inputs(series(6, 40, 60, [](b::RuntimeSample &s) {
        s.cpu_busiest_core = 0.98;
        s.cpu_freq_ratio = 1.0;
    })));
    CHECK(nogpu.get(b::Kind::Cpu)->state == b::State::Likely);
    // Busy but not at max frequency is not a CPU limit (the governor has headroom).
    auto headroom = b::assess(inputs(series(6, 40, 60, [](b::RuntimeSample &s) {
        s.cpu_busiest_core = 0.98;
        s.cpu_freq_ratio = 0.6;
    })));
    CHECK(headroom.get(b::Kind::Cpu)->state <= b::State::Possible);
}

void test_gpu_evidence() {
    auto a = b::assess(inputs(series(6, 45, 60, gpu_bound)));
    check_contract(a);
    auto *gpu = a.get(b::Kind::Gpu);
    CHECK(gpu && gpu->state == b::State::Confirmed);
    CHECK(a.primary == b::Kind::Gpu);
    bool has_busy = false;
    for (auto &e : gpu->evidence)
        if (e.metric == "gpu_busy") has_busy = true;
    CHECK(has_busy);
    // Four samples: sustained but under kConfirmSamples -> LIKELY.
    auto shorter = b::assess(inputs(series(4, 45, 60, gpu_bound)));
    CHECK(shorter.get(b::Kind::Gpu)->state == b::State::Likely);
    // GPU load interface known absent from the capability context: GPU stays UNKNOWN without samples.
    ctx::CapabilityContext c;
    ctx::CapabilityFact f;
    f.id = "graphics.gpu.load_interface";
    f.domain = "graphics";
    f.support = ctx::Support::No;
    f.confidence = ctx::Confidence::Medium;
    c.publish("graphics", {f});
    auto in = inputs(series(6, 40, 60, [](b::RuntimeSample &s) { s.cpu_busiest_core = 0.5; }));
    in.context = &c;
    auto u = b::assess(in);
    CHECK(u.get(b::Kind::Gpu)->state == b::State::Unknown);
    CHECK(u.get(b::Kind::Gpu)->note.find("load interface") != std::string::npos);
}

struct FakeThermal : b::ThermalContext {
    double cap;
    bool throttle;
    FakeThermal(double c, bool t) : cap(c), throttle(t) {}
    std::optional<b::ThermalState> at(int64_t t) const override { return b::ThermalState{t, throttle, cap, "synrei"}; }
};

void test_thermal_evidence() {
    FakeThermal hot(0.7, true);
    auto in = inputs(series(6, 40, 60, cpu_bound));
    in.thermal = &hot;
    auto a = b::assess(in);
    check_contract(a);
    auto *th = a.get(b::Kind::Thermal);
    CHECK(th && th->state == b::State::Confirmed && th->source == "synrei");
    // A thermal cap explains the CPU saturation: thermal is primary, not a conflict.
    CHECK(a.primary == b::Kind::Thermal && !a.conflict);
    CHECK(a.get(b::Kind::Cpu)->state >= b::State::Likely);

    // No thermal interface: UNKNOWN, never assumed cool.
    auto none = b::assess(inputs(series(6, 40, 60, cpu_bound)));
    CHECK(none.get(b::Kind::Thermal)->state == b::State::Unknown);
    CHECK(none.get(b::Kind::Thermal)->note.find("thermal context") != std::string::npos);

    // Throttling reported but frames on target: POSSIBLE only.
    auto met = inputs(series(6, 60, 60, nullptr));
    met.thermal = &hot;
    CHECK(b::assess(met).get(b::Kind::Thermal)->state == b::State::Possible);

    // Uncapped thermal state is evidence *against* thermal, state stays UNKNOWN/POSSIBLE-free.
    FakeThermal cool(1.0, false);
    auto c = inputs(series(6, 40, 60, cpu_bound));
    c.thermal = &cool;
    auto ca = b::assess(c);
    CHECK(ca.get(b::Kind::Thermal)->state == b::State::Unknown);
    CHECK(ca.primary == b::Kind::Cpu);
}

void test_conflicting_evidence() {
    auto a = b::assess(inputs(series(6, 40, 60, both_bound)));
    check_contract(a);
    CHECK(a.conflict);
    CHECK(a.primary == b::Kind::Unknown);
    CHECK(a.primary_state == b::State::Unknown);
    // Neither may be CONFIRMED while the other is at least LIKELY.
    CHECK(a.get(b::Kind::Cpu)->state == b::State::Likely);
    CHECK(a.get(b::Kind::Gpu)->state == b::State::Likely);
    CHECK(a.note.find("cpu") != std::string::npos && a.note.find("gpu") != std::string::npos);

    // Mixed samples (half CPU, half GPU): neither reaches LIKELY.
    auto mixed = series(6, 40, 60, nullptr);
    for (size_t i = 0; i < mixed.size(); ++i) (i % 2 ? cpu_bound : gpu_bound)(mixed[i]);
    auto m = b::assess(inputs(mixed));
    CHECK(m.get(b::Kind::Cpu)->state <= b::State::Possible);
    CHECK(m.get(b::Kind::Gpu)->state <= b::State::Possible);
    CHECK(m.primary == b::Kind::Unknown);
}

void test_insufficient_data() {
    // No samples at all.
    auto empty = b::assess(inputs({}));
    check_contract(empty);
    for (auto &o : empty.observations) CHECK(o.state == b::State::Unknown);
    CHECK(empty.primary == b::Kind::Unknown);
    CHECK(empty.note.find("insufficient") != std::string::npos);

    // Two samples of perfect CPU evidence: at most POSSIBLE.
    auto two = b::assess(inputs(series(2, 40, 60, cpu_bound)));
    CHECK(two.get(b::Kind::Cpu)->state == b::State::Possible);
    CHECK(two.primary == b::Kind::Unknown);

    // Utilisation without FPS: saturation is seen, but no deficit can be shown -> POSSIBLE.
    auto nofps = series(6, 0, 60, cpu_bound);
    for (auto &s : nofps) s.fps.reset();
    auto n = b::assess(inputs(nofps));
    CHECK(n.get(b::Kind::Cpu)->state == b::State::Possible);

    // FPS only: a deficit exists but nothing explains it -> all UNKNOWN.
    auto fpsonly = b::assess(inputs(series(6, 30, 60, nullptr)));
    for (auto &o : fpsonly.observations) CHECK(o.state == b::State::Unknown);
    CHECK(fpsonly.primary == b::Kind::Unknown);
}

void test_memory_storage_display() {
    auto mem = b::assess(inputs(series(6, 40, 60, [](b::RuntimeSample &s) {
        s.mem_psi_some = 25;
        s.cpu_busiest_core = 0.4;
        s.gpu_busy = 0.4;
    })));
    CHECK(mem.get(b::Kind::Memory)->state >= b::State::Likely);
    auto io = b::assess(inputs(series(6, 40, 60, [](b::RuntimeSample &s) { s.io_psi_some = 35; })));
    CHECK(io.get(b::Kind::Storage)->state >= b::State::Likely);

    // Frames pinned at 60 on a panel that also offers 120: display-bound (LIKELY/CONFIRMED).
    ctx::CapabilityContext c;
    auto disp = [](const std::string &id, const std::string &v) {
        ctx::CapabilityFact f;
        f.id = id;
        f.domain = "display";
        f.support = ctx::Support::Yes;
        f.readable = true;
        f.confidence = ctx::Confidence::High;
        f.value = v;
        return f;
    };
    c.publish("display", {disp("display.refresh.current_hz", "60"), disp("display.refresh.max_hz", "120")});
    auto in = inputs(series(6, 59.8, 60, [](b::RuntimeSample &s) {
        s.cpu_busiest_core = 0.5;
        s.gpu_busy = 0.5;
    }));
    in.context = &c;
    auto d = b::assess(in);
    CHECK(d.get(b::Kind::Display)->state == b::State::Confirmed);
    CHECK(d.primary == b::Kind::Display);
    bool from_ctx = false;
    for (auto &e : d.get(b::Kind::Display)->evidence)
        if (e.source == "capability_context") from_ctx = true;
    CHECK(from_ctx);
    // Performance state is carried as evidence, not acted on.
    bool from_perf = false;
    for (auto &e : d.get(b::Kind::Display)->evidence)
        if (e.source == "performance_state") from_perf = true;
    CHECK(from_perf);
}

void test_facts() {
    auto a = b::assess(inputs(series(6, 40, 60, cpu_bound)));
    auto facts = b::to_facts(a);
    CHECK_EQ(facts.size(), size_t(7)); // 6 kinds + primary
    for (auto &f : facts) {
        CHECK_EQ(f.domain, std::string("bottleneck"));
        CHECK(!f.writable);
    }
    CHECK_EQ(std::string(b::to_string(b::State::Confirmed)), std::string("confirmed"));
    CHECK_EQ(std::string(b::to_string(b::Kind::Storage)), std::string("storage"));
}

} // namespace

int main() {
    test_cpu_evidence();
    test_gpu_evidence();
    test_thermal_evidence();
    test_conflicting_evidence();
    test_insufficient_data();
    test_memory_storage_display();
    test_facts();
    return flux_test::report("bottleneck_model_test");
}
