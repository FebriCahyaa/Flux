// FPS observation bridge (Step 8.8.1): available, missing, stale, ordering, target refresh
// integration, bottleneck receives FPS (thresholds unchanged), slot semantics, no second loop.
#include "flux_test.hpp"
#include "FpsObservation.hpp"
#include "RuntimeMetricsSampler.hpp"

#include <atomic>
#include <cmath>
#include <set>
#include <thread>

namespace b = flux::bottleneck;
namespace ctx = flux::context;
namespace k = flux::kernel;
namespace m = flux::metrics;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
    Kind kind(const std::string &p) const override {
        if (files.count(p)) return Kind::File;
        auto it = files.lower_bound(p + "/");
        return it != files.end() && it->first.rfind(p + "/", 0) == 0 ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &p) const override {
        if (!files.count(p)) return std::nullopt;
        return files.at(p);
    }
    std::vector<std::string> list(const std::string &d) const override {
        std::set<std::string> out;
        for (auto &[p, _] : files)
            if (p.rfind(d + "/", 0) == 0) {
                auto rest = p.substr(d.size() + 1);
                out.insert(rest.substr(0, rest.find('/')));
            }
        return {out.begin(), out.end()};
    }
    bool writable_hint(const std::string &) const override { return false; }
};

std::string stat(int busy, int idle) {
    return "cpu  " + std::to_string(busy) + " 0 0 " + std::to_string(idle) + " 0 0 0 0\ncpu0 " + std::to_string(busy) +
           " 0 0 " + std::to_string(idle) + " 0 0 0 0\n";
}

FakeFs cpu_bound_device() {
    FakeFs f;
    f.files["proc/stat"] = stat(100, 900);
    f.files["sys/devices/system/cpu/cpufreq/policy0/related_cpus"] = "0\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq"] = "2000000\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq"] = "2000000\n";
    f.files["sys/class/kgsl/kgsl-3d0/gpu_busy_percentage"] = "30 %\n";
    return f;
}

ctx::CapabilityContext display_context(const std::string &hz) {
    ctx::CapabilityContext c;
    ctx::CapabilityFact f;
    f.id = "display.refresh.current_hz";
    f.domain = "display";
    f.support = ctx::Support::Yes;
    f.readable = true;
    f.confidence = ctx::Confidence::High;
    f.value = hz;
    c.publish("display", {f});
    return c;
}

void test_accept_rules() {
    m::FpsObservation ok{1000, 58.5, true, "game"};
    auto a = m::accept_fps(ok, 1500, 3000, 0);
    CHECK(a.fps && *a.fps == 58.5 && a.reason == "accepted");
    // No observation at all.
    auto none = m::accept_fps(std::nullopt, 1500, 3000, 0);
    CHECK(!none.fps && none.reason.find("no observation") != std::string::npos);
    // Invalid (recorder had no reading).
    m::FpsObservation invalid{1000, std::nullopt, false, ""};
    CHECK(!m::accept_fps(invalid, 1500, 3000, 0).fps);
    // Stale.
    auto stale = m::accept_fps(ok, 1000 + 3001, 3000, 0);
    CHECK(!stale.fps && stale.reason.find("stale") != std::string::npos);
    CHECK(m::accept_fps(ok, 1000 + 3000, 3000, 0).fps); // boundary still fresh
    // Timestamp ordering: future, older and repeated observations are rejected.
    auto future = m::accept_fps(ok, 900, 3000, 0);
    CHECK(!future.fps && future.reason.find("future") != std::string::npos);
    auto repeat = m::accept_fps(ok, 1500, 3000, 1000);
    CHECK(!repeat.fps && repeat.reason.find("not newer") != std::string::npos);
    CHECK(!m::accept_fps(ok, 1500, 3000, 1200).fps);
}

void test_slot() {
    m::FpsObservationSlot slot;
    CHECK(!slot.latest());
    slot.publish(10, 60.0, "fpsgo");
    CHECK(slot.latest() && *slot.latest()->fps == 60.0 && slot.latest()->valid && slot.latest()->source == "fpsgo");
    slot.publish(11, std::nan(""), "");
    CHECK(slot.latest() && !slot.latest()->valid && !slot.latest()->fps);
    slot.publish(12, 0.0, "game");
    CHECK(!slot.latest()->valid);
    slot.clear();
    CHECK(!slot.latest());
    // Writer thread + reader thread: no torn values (fps always equals the published timestamp).
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int i = 1; i < 20000; ++i) slot.publish(i, double(i), "t");
        stop = true;
    });
    bool consistent = true;
    while (!stop)
        if (auto o = slot.latest(); o && o->fps && *o->fps != double(o->timestamp_ms)) consistent = false;
    writer.join();
    CHECK(consistent);
}

void test_sampler_uses_observation() {
    auto f = cpu_bound_device();
    m::FpsObservationSlot slot;
    auto c = display_context("60");
    m::RuntimeMetricsSampler sampler(f, &c, {1000, 20}, [&] { return slot.latest(); });

    // No observation yet: FPS UNKNOWN, recorded as such.
    sampler.start("s", 1000);
    CHECK(!sampler.window().back().fps);
    auto *fm = sampler.last_snapshot()->get("fps");
    CHECK(fm && !fm->readable && fm->note.find("no observation") != std::string::npos);
    CHECK(sampler.window().back().target_hz && *sampler.window().back().target_hz == 60);

    // Fresh observation: accepted with its own timestamp and source.
    slot.publish(1900, 41.0, "game");
    f.files["proc/stat"] = stat(200, 900);
    sampler.tick(2000);
    auto &w = sampler.window().back();
    CHECK(w.fps && *w.fps == 41.0);
    auto *f2 = sampler.last_snapshot()->get("fps");
    CHECK(f2 && f2->readable && f2->source == "session_recorder:game" && f2->timestamp_ms == 1900);
    CHECK(f2 && f2->confidence == ctx::Confidence::High && !f2->verified);

    // Same observation again (recorder did not publish): not reused.
    f.files["proc/stat"] = stat(300, 900);
    sampler.tick(3000);
    CHECK(!sampler.window().back().fps);
    CHECK(sampler.last_snapshot()->get("fps")->note.find("not newer") != std::string::npos);

    // Stale observation (recorder stalled for 5 s): UNKNOWN.
    slot.publish(3500, 41.0, "game");
    f.files["proc/stat"] = stat(400, 900);
    sampler.tick(9000);
    CHECK(!sampler.window().back().fps);
    CHECK(sampler.last_snapshot()->get("fps")->note.find("stale") != std::string::npos);
}

std::vector<b::RuntimeSample> run_series(bool with_fps) {
    auto f = cpu_bound_device();
    m::FpsObservationSlot slot;
    auto c = display_context("60");
    m::RuntimeMetricsSampler sampler(f, &c, {1000, 20},
                                     with_fps ? m::FpsSource([&] { return slot.latest(); }) : m::FpsSource());
    sampler.start("s", 0);
    for (int i = 1; i <= 7; ++i) {
        f.files["proc/stat"] = stat(100 + i * 100, 900); // busiest core 100 % every interval
        slot.publish(i * 1000 - 100, 40.0, "game");     // 40 FPS against a 60 Hz target
        sampler.tick(i * 1000);
    }
    return {sampler.window().begin(), sampler.window().end()};
}

void test_bottleneck_receives_fps() {
    b::BottleneckInputs with;
    with.session.samples = run_series(true);
    with.now_ms = 8000;
    auto a = b::assess(with);
    auto *cpu = a.get(b::Kind::Cpu);
    CHECK(cpu && cpu->state == b::State::Confirmed); // FPS shortfall + GPU idle + sustained
    bool deficit = false;
    for (auto &e : cpu->evidence)
        if (e.metric == "frame_deficit") deficit = true;
    CHECK(deficit);
    CHECK(a.primary == b::Kind::Cpu);

    // Same device without FPS: thresholds unchanged, so no shortfall evidence -> POSSIBLE only.
    b::BottleneckInputs without;
    without.session.samples = run_series(false);
    without.now_ms = 8000;
    auto n = b::assess(without);
    CHECK(n.get(b::Kind::Cpu)->state == b::State::Possible);
    CHECK(n.primary == b::Kind::Unknown);
    for (auto &s : without.session.samples) CHECK(!s.fps); // never estimated from load
}

void test_no_second_loop() {
    // The sampler reads FPS only when it samples: one read per accepted tick, none between.
    auto f = cpu_bound_device();
    int reads = 0;
    m::RuntimeMetricsSampler sampler(f, nullptr, {2000, 10}, [&] {
        ++reads;
        return std::optional<m::FpsObservation>();
    });
    sampler.start("s", 0);
    for (int t = 100; t <= 10000; t += 100) sampler.tick(t);
    CHECK_EQ(reads, int(sampler.samples_taken()));
    sampler.stop(10000);
    for (int t = 10100; t <= 20000; t += 100) sampler.tick(t);
    CHECK_EQ(reads, int(sampler.samples_taken())); // nothing after stop
}

} // namespace

int main() {
    test_accept_rules();
    test_slot();
    test_sampler_uses_observation();
    test_bottleneck_receives_fps();
    test_no_second_loop();
    return flux_test::report("fps_observation_test");
}
