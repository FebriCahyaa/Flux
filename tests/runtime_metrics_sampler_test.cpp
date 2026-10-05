// Runtime metrics sampling lifecycle (Step 8.8): session start/stop, duplicates, process death,
// daemon stop, first/second sample, UNKNOWN preservation, failure isolation, read-only, bounds.
#include "flux_test.hpp"
#include "RuntimeMetricsSampler.hpp"

#include <set>
#include <stdexcept>

namespace k = flux::kernel;
namespace m = flux::metrics;
namespace s = flux::session;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
    mutable int reads = 0;
    bool explode = false;
    Kind kind(const std::string &p) const override {
        if (explode) throw std::runtime_error("fs failure");
        if (files.count(p)) return Kind::File;
        auto it = files.lower_bound(p + "/");
        return it != files.end() && it->first.rfind(p + "/", 0) == 0 ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &p) const override {
        ++reads;
        if (explode) throw std::runtime_error("fs failure");
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
    bool writable_hint(const std::string &) const override { return true; }
};

std::string stat(int busy, int idle) {
    return "cpu  " + std::to_string(busy) + " 0 0 " + std::to_string(idle) + " 0 0 0 0\ncpu0 " + std::to_string(busy) +
           " 0 0 " + std::to_string(idle) + " 0 0 0 0\n";
}

FakeFs device() {
    FakeFs f;
    f.files["proc/stat"] = stat(100, 900);
    f.files["proc/meminfo"] = "MemTotal: 4000000 kB\nMemAvailable: 1000000 kB\n";
    f.files["sys/block/sda/stat"] = "100 0 0 100 100 0 0 100 0 0 0\n";
    return f;
}

/// Records the order of participant calls next to the sampler participant.
struct Recorder : s::SessionParticipant {
    std::vector<std::string> log;
    const char *name() const override { return "recorder"; }
    void begin(const s::SessionInfo &) override { log.push_back("begin"); }
    bool end(const s::SessionInfo &, s::EndReason) override {
        log.push_back("end");
        return true;
    }
};

s::SessionKey key(const std::string &pkg, int pid) {
    s::SessionKey k;
    k.package = pkg;
    k.pid = pid;
    return k;
}

void test_lifecycle_start_duplicate_end() {
    auto f = device();
    m::RuntimeMetricsSampler sampler(f, nullptr, {2000, 10});
    m::SamplerParticipant part(sampler);
    Recorder rec;
    s::SessionManager mgr;
    mgr.add(&rec);
    mgr.add(&part);
    mgr.recover();

    CHECK(!sampler.running());
    CHECK(!mgr.needs_tick()); // no session: no tick, no sampling
    mgr.tick(500);
    CHECK_EQ(sampler.samples_taken(), uint64_t(0));

    CHECK(mgr.begin(key("com.game", 10), 1000));
    CHECK(sampler.running());
    CHECK(mgr.needs_tick());
    CHECK_EQ(sampler.session_id(), mgr.current().id);
    CHECK_EQ(sampler.samples_taken(), uint64_t(1)); // first sample at start

    // Duplicate begin for the same process: no new session, no second sampler start.
    CHECK(!mgr.begin(key("com.game", 10), 1500));
    CHECK(!sampler.start(mgr.current().id, 1500));
    CHECK_EQ(sampler.samples_taken(), uint64_t(1));

    // Interval respected: ticks before 2000 ms do not sample.
    mgr.tick(2000);
    CHECK_EQ(sampler.samples_taken(), uint64_t(1));
    mgr.tick(3000);
    CHECK_EQ(sampler.samples_taken(), uint64_t(2));

    CHECK(mgr.end(s::EndReason::Exit, 4000));
    CHECK(!sampler.running());
    CHECK(!mgr.needs_tick());
    CHECK(sampler.final_assessment().has_value());
    // No runaway sampling after the session ended.
    sampler.tick(100000);
    mgr.tick(100000);
    CHECK_EQ(sampler.samples_taken(), uint64_t(2));
}

void test_stop_reasons() {
    for (auto why : {s::EndReason::ProcessDeath, s::EndReason::DaemonStop, s::EndReason::FocusLost,
                     s::EndReason::Failure}) {
        auto f = device();
        m::RuntimeMetricsSampler sampler(f, nullptr);
        m::SamplerParticipant part(sampler);
        s::SessionManager mgr;
        mgr.add(&part);
        mgr.recover();
        mgr.begin(key("com.game", 20), 1000);
        CHECK(sampler.running());
        CHECK(mgr.end(why, 5000));
        CHECK(!sampler.running());
        CHECK(sampler.state() == m::RuntimeMetricsSampler::State::Idle);
    }
    // Switch to another game: one sampler, now for the new session, samples restart.
    auto f = device();
    m::RuntimeMetricsSampler sampler(f, nullptr);
    m::SamplerParticipant part(sampler);
    s::SessionManager mgr;
    mgr.add(&part);
    mgr.recover();
    mgr.begin(key("com.a", 1), 1000);
    auto first = sampler.session_id();
    mgr.begin(key("com.b", 2), 2000);
    CHECK(sampler.running());
    CHECK(sampler.session_id() != first);
    CHECK_EQ(sampler.samples_taken(), uint64_t(1));
}

void test_first_and_second_sample() {
    auto f = device();
    m::RuntimeMetricsSampler sampler(f, nullptr, {1000, 10});
    sampler.start("s-1", 1000);
    auto *util = sampler.last_snapshot()->get("cpu.utilization.busiest_core");
    CHECK(util && !util->readable && util->note.find("previous sample") != std::string::npos);
    CHECK(!sampler.window().back().cpu_busiest_core);
    auto *lat = sampler.last_snapshot()->get("io.sda.latency_ms");
    CHECK(lat && !lat->readable);

    f.files["proc/stat"] = stat(250, 950); // +150 busy of +200
    f.files["sys/block/sda/stat"] = "200 0 0 300 200 0 0 300 0 0 0\n";
    sampler.tick(2000);
    auto *u2 = sampler.last_snapshot()->get("cpu.utilization.busiest_core");
    CHECK(u2 && u2->readable && *u2->value == 0.75);
    CHECK_EQ(u2->timestamp_ms, int64_t(2000));
    CHECK(u2->source == "proc/stat" && u2->confidence != flux::context::Confidence::None);
    auto *l2 = sampler.last_snapshot()->get("io.sda.latency_ms");
    CHECK(l2 && l2->readable && *l2->value == 2.0);
    CHECK(sampler.window().back().cpu_busiest_core && *sampler.window().back().cpu_busiest_core == 0.75);
    CHECK_EQ(sampler.window().back().timestamp_ms, int64_t(2000));

    // A new session starts with a fresh collector: first sample UNKNOWN again.
    sampler.stop(2500);
    sampler.start("s-2", 3000);
    CHECK(!sampler.last_snapshot()->get("cpu.utilization.busiest_core")->readable);
    CHECK_EQ(sampler.window().size(), size_t(1));
}

void test_missing_metric_unknown() {
    FakeFs empty;
    m::RuntimeMetricsSampler sampler(empty, nullptr, {1000, 10});
    sampler.start("s", 0);
    sampler.tick(1000);
    auto &snap = *sampler.last_snapshot();
    for (auto id : {"gpu.utilization", "mem.available_mb", "io.psi_some_avg10"}) {
        auto *x = snap.get(id);
        CHECK(x && !x->readable && !x->value && !x->verified);
    }
    auto &w = sampler.window().back();
    CHECK(!w.gpu_busy && !w.mem_available_mb && !w.io_psi_some && !w.fps);
    auto a = sampler.assess(2000);
    CHECK(a.primary == flux::bottleneck::Kind::Unknown);
}

void test_failure_isolated() {
    auto f = device();
    m::RuntimeMetricsSampler sampler(f, nullptr, {1000, 10});
    std::vector<m::SampleNotice> notes;
    sampler.set_observer([&](const m::SampleNotice &n) { notes.push_back(n); });
    m::SamplerParticipant part(sampler);
    Recorder rec;
    s::SessionManager mgr;
    mgr.add(&part);
    mgr.add(&rec);
    mgr.recover();
    CHECK(mgr.begin(key("com.game", 3), 1000));
    f.explode = true;
    for (int i = 1; i <= 5; ++i) mgr.tick(1000 + i * 1000); // never throws out of tick
    CHECK(mgr.active());                                   // the game session survives
    CHECK(sampler.state() == m::RuntimeMetricsSampler::State::Failed);
    CHECK(!mgr.needs_tick());                              // no further sampling ticks
    int failed = 0;
    for (auto &n : notes) failed += n.failed;
    CHECK_EQ(failed, m::kMaxConsecutiveFailures);
    CHECK(mgr.end(s::EndReason::Exit, 9000));              // clean end, participants still ran
    CHECK_EQ(rec.log.back(), std::string("end"));
    CHECK(sampler.state() == m::RuntimeMetricsSampler::State::Idle);

    // Collector failure at start also leaves the session intact.
    f.explode = true;
    CHECK(mgr.begin(key("com.game", 4), 10000));
    CHECK(mgr.active());
    // A throwing observer is swallowed.
    f.explode = false;
    sampler.set_observer([](const m::SampleNotice &) { throw 1; });
    sampler.start("x", 20000);
    sampler.tick(21000);
    CHECK(sampler.running());
}

void test_read_only_and_bounds() {
    auto f = device();
    auto before = f.files;
    m::RuntimeMetricsSampler sampler(f, nullptr, {1, 1000000}); // clamped
    CHECK_EQ(sampler.config().interval_ms, m::kMinIntervalMs);
    CHECK_EQ(sampler.config().window, m::kMaxWindow);
    sampler.start("s", 0);
    for (int i = 1; i <= 2000; ++i) sampler.tick(i * 1000);
    CHECK(sampler.window().size() <= m::kMaxWindow);
    CHECK(f.files == before); // no node modified
    auto c = m::clamp({999999999, 0});
    CHECK_EQ(c.interval_ms, m::kMaxIntervalMs);
    CHECK_EQ(c.window, m::kMinWindow);
}

void test_forward_to_bottleneck() {
    auto f = device();
    double fps = 40;
    int64_t fps_ts = 0;
    m::RuntimeMetricsSampler sampler(f, nullptr, {1000, 10}, [&] {
        return std::optional<m::FpsObservation>(m::FpsObservation{fps_ts, fps, true, "game"});
    });
    f.files["sys/devices/system/cpu/cpufreq/policy0/related_cpus"] = "0\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq"] = "2000000\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq"] = "2000000\n";
    sampler.start("s", 0);
    for (int i = 1; i <= 6; ++i) {
        f.files["proc/stat"] = stat(100 + i * 100, 900); // 100 % busy each interval
        fps_ts = i * 1000 - 50;
        sampler.tick(i * 1000);
    }
    auto &last = sampler.window().back();
    CHECK(last.fps && *last.fps == 40);
    auto a = sampler.assess(7000);
    CHECK(a.get(flux::bottleneck::Kind::Cpu)->state >= flux::bottleneck::State::Possible);
    CHECK_EQ(a.samples, sampler.window().size());
}

} // namespace

int main() {
    test_lifecycle_start_duplicate_end();
    test_stop_reasons();
    test_first_and_second_sample();
    test_missing_metric_unknown();
    test_failure_isolated();
    test_read_only_and_bounds();
    test_forward_to_bottleneck();
    return flux_test::report("runtime_metrics_sampler_test");
}
