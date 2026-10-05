// Synrei thermal context (Step 8.9): available, unavailable, missing temperature, unreadable,
// stale, constraint / normal state, confidence, failure isolation, bottleneck evidence, zero writes.
#include "flux_test.hpp"
#include "RuntimeMetricsSampler.hpp"
#include "SynreiThermalAdapter.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace b = flux::bottleneck;
namespace ctx = flux::context;
namespace k = flux::kernel;
namespace m = flux::metrics;
namespace t = flux::thermal;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
    std::set<std::string> denied;
    mutable std::set<std::string> read_paths;
    bool explode = false;
    Kind kind(const std::string &p) const override {
        if (explode) throw std::runtime_error("io");
        if (files.count(p)) return Kind::File;
        auto it = files.lower_bound(p + "/");
        return it != files.end() && it->first.rfind(p + "/", 0) == 0 ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &p) const override {
        if (explode) throw std::runtime_error("io");
        read_paths.insert(p);
        if (denied.count(p) || !files.count(p)) return std::nullopt;
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

int64_t wall = 1'700'000'000;

std::string state_file(const std::string &state, const std::string &cpu, const std::string &gpu,
                       const std::string &battery, int64_t updated, int pid = 4242) {
    return "state=" + state + "\nreason=test\ngame=com.game\nsince=" + std::to_string(updated - 60) +
           "\nupdated=" + std::to_string(updated) + "\nflux=running\ncpu_temp=" + cpu + "\ngpu_temp=" + gpu +
           "\nbattery_temp=" + battery + "\nlevel=max\npid=" + std::to_string(pid) + "\nversion=2.1.0\n";
}

FakeFs synrei(const std::string &state, const std::string &cpu = "71.5", const std::string &gpu = "64.0",
              const std::string &battery = "38.2") {
    FakeFs f;
    f.files[t::kSynreiStatePath] = state_file(state, cpu, gpu, battery, wall);
    f.files["proc/4242/stat"] = "4242 (hicod) S 1\n"; // process alive
    return f;
}

t::SynreiThermalAdapter adapter(const FakeFs &f) {
    return t::SynreiThermalAdapter(f, [] { return wall; }, 15);
}

void test_available() {
    auto f = synrei("boost");
    auto a = adapter(f);
    auto s = a.read(5000);
    CHECK(s.readable && s.verified);
    CHECK_EQ(s.state, std::string("boost"));
    CHECK(s.constraint == t::Constraint::Unconstrained);
    CHECK(s.cpu.readable && *s.cpu.celsius == 71.5);
    CHECK(s.gpu.readable && *s.gpu.celsius == 64.0);
    CHECK(s.battery.readable && *s.battery.celsius == 38.2);
    CHECK(!s.headroom_c); // not published by Synrei: UNKNOWN, not inferred
    CHECK(!s.slope_c_per_min); // needs a previous verified reading
    CHECK_EQ(s.timestamp_ms, int64_t(5000));
    CHECK_EQ(s.source_time_s, wall);
    CHECK(s.source == "synrei:/dev/hico/state");
    CHECK(s.confidence == ctx::Confidence::High);
    CHECK(s.usable());

    // Slope from the next verified reading of the same Synrei process: +3 °C in 30 s = 6 °C/min.
    wall += 30;
    f.files[t::kSynreiStatePath] = state_file("boost", "74.5", "64.0", "38.2", wall);
    auto s2 = a.read(35000);
    CHECK(s2.slope_c_per_min && *s2.slope_c_per_min == 6.0);
    wall -= 30;
}

void test_unavailable() {
    FakeFs none; // hicod not running (state file removed on shutdown)
    auto s = adapter(none).read(1000);
    CHECK(!s.readable && !s.verified && !s.usable());
    CHECK(s.constraint == t::Constraint::Unknown);
    CHECK(s.confidence == ctx::Confidence::None);
    CHECK(s.note.find("not running") != std::string::npos);
    CHECK(!s.cpu.celsius && !s.gpu.celsius && !s.battery.celsius);

    // State file left behind by a crashed hicod (pid gone): not current evidence.
    auto crashed = synrei("safety");
    crashed.files.erase("proc/4242/stat");
    auto c = adapter(crashed).read(1000);
    CHECK(!c.verified && c.constraint == t::Constraint::Unknown && !c.usable());
    CHECK(c.note.find("process") != std::string::npos);
    CHECK(!c.cpu.celsius);
}

void test_missing_temperature() {
    auto f = synrei("boost", "71.0", "", "");
    auto s = adapter(f).read(1000);
    CHECK(s.cpu.readable);
    CHECK(!s.gpu.readable && !s.gpu.celsius && s.gpu.note.find("not reported") != std::string::npos);
    CHECK(!s.battery.readable && !s.battery.celsius);
    CHECK(s.usable()); // the constraint comes from the state, not from temperatures
    // Implausible value is malformed, not clamped.
    auto bad = synrei("boost", "999.0", "abc", "38.0");
    auto sb = adapter(bad).read(1000);
    CHECK(!sb.cpu.readable && sb.cpu.note.find("malformed") != std::string::npos);
    CHECK(!sb.gpu.readable);
    CHECK(sb.battery.readable);
}

void test_unreadable_source() {
    auto f = synrei("safety");
    f.denied.insert(t::kSynreiStatePath);
    auto s = adapter(f).read(1000);
    CHECK(!s.readable && !s.verified && s.constraint == t::Constraint::Unknown);
    CHECK(s.note.find("unreadable") != std::string::npos);
    // Malformed content (no state key).
    auto g = synrei("safety");
    g.files[t::kSynreiStatePath] = "garbage without keys\n";
    auto sg = adapter(g).read(1000);
    CHECK(!sg.readable && sg.constraint == t::Constraint::Unknown && sg.note.find("malformed") != std::string::npos);
}

void test_stale() {
    auto f = synrei("safety");
    f.files[t::kSynreiStatePath] = state_file("safety", "90.0", "80.0", "45.0", wall - 16); // older than 15 s
    auto s = adapter(f).read(1000);
    CHECK(s.readable && !s.verified && !s.usable());
    CHECK(s.constraint == t::Constraint::Unknown);
    CHECK(s.note.find("stale") != std::string::npos);
    CHECK(!s.cpu.celsius); // stale values are not exposed as current
    // Updated in the future (clock jump): not verified either.
    f.files[t::kSynreiStatePath] = state_file("safety", "90.0", "80.0", "45.0", wall + 120);
    CHECK(!adapter(f).read(1000).verified);
    // Boundary: exactly 15 s old is still fresh.
    f.files[t::kSynreiStatePath] = state_file("safety", "90.0", "80.0", "45.0", wall - 15);
    CHECK(adapter(f).read(1000).verified);
}

void test_constraint_mapping() {
    CHECK(adapter(synrei("safety")).read(1).constraint == t::Constraint::Constrained);
    CHECK(adapter(synrei("boost")).read(1).constraint == t::Constraint::Unconstrained);
    // Hot device in a state where Synrei does not report throttling: never inferred.
    for (auto st : {"idle", "relaxed", "suspended", "disabled", "whatever"}) {
        auto s = adapter(synrei(st, "96.0", "90.0", "47.0")).read(1);
        CHECK(s.constraint == t::Constraint::Unknown);
        CHECK(s.readable && s.verified); // valid context, just no constraint claim
        CHECK(*s.cpu.celsius == 96.0);
        CHECK(!s.usable());
    }
    // Boost while 96 °C is still Unconstrained: Synrei's state is authoritative.
    CHECK(adapter(synrei("boost", "96.0")).read(1).constraint == t::Constraint::Unconstrained);
}

void test_confidence_and_history() {
    t::ThermalHistory h(5000);
    auto s = adapter(synrei("safety")).read(10000);
    CHECK(s.confidence == ctx::Confidence::High);
    h.add(s);
    auto e = h.at(12000);
    CHECK(e && e->throttling && e->source == "synrei" && e->timestamp_ms == 10000);
    CHECK(!h.at(9000));          // nothing at or before
    CHECK(!h.at(15001));         // gap > 5 s: stale for this sample
    auto boost = adapter(synrei("boost")).read(16000);
    h.add(boost);
    auto e2 = h.at(16500);
    CHECK(e2 && !e2->throttling);
    // A newer snapshot supersedes older ones: once Synrei reports a state without a constraint
    // claim, the earlier boost is no longer current evidence.
    h.add(adapter(synrei("relaxed")).read(17000));
    CHECK(!h.at(17500));
    CHECK(h.at(16900) && !h.at(16900)->throttling); // before the relaxed snapshot: boost
    CHECK_EQ(std::string(t::to_string(t::Constraint::Constrained)), std::string("constrained"));
}

std::string stat(int busy, int idle) {
    return "cpu  " + std::to_string(busy) + " 0 0 " + std::to_string(idle) + " 0 0 0 0\ncpu0 " + std::to_string(busy) +
           " 0 0 " + std::to_string(idle) + " 0 0 0 0\n";
}

ctx::CapabilityContext display60() {
    ctx::CapabilityContext c;
    ctx::CapabilityFact f;
    f.id = "display.refresh.current_hz";
    f.domain = "display";
    f.support = ctx::Support::Yes;
    f.readable = true;
    f.confidence = ctx::Confidence::High;
    f.value = "60";
    c.publish("display", {f});
    return c;
}

void test_bottleneck_receives_thermal() {
    auto display = display60(); // target refresh from the CapabilityContext
    auto f = synrei("safety");
    f.files["proc/stat"] = stat(100, 900);
    f.files["sys/devices/system/cpu/cpufreq/policy0/related_cpus"] = "0\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq"] = "2000000\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq"] = "2000000\n";
    auto syn = adapter(f);
    int64_t fps_ts = 0;
    m::RuntimeMetricsSampler sampler(
        f, &display, {1000, 20},
        [&] { return std::optional<m::FpsObservation>(m::FpsObservation{fps_ts, 40.0, true, "game"}); },
        [&](int64_t now) { return syn.read(now); });
    sampler.start("s", 0);
    for (int i = 1; i <= 7; ++i) {
        f.files["proc/stat"] = stat(100 + i * 100, 900);
        f.files[t::kSynreiStatePath] = state_file("safety", "88.0", "80.0", "44.0", wall);
        fps_ts = i * 1000 - 100;
        sampler.tick(i * 1000);
    }
    CHECK(sampler.last_snapshot()->get("thermal.constraint")->text == "constrained");
    CHECK(sampler.last_snapshot()->get("thermal.cpu_temp_c")->readable);
    CHECK(!sampler.last_snapshot()->get("thermal.headroom_c")->readable);
    auto a = sampler.assess(8000, {"performance", false, 0}); // no thermal passed: sampler's history
    auto *th = a.get(b::Kind::Thermal);
    CHECK(th && th->state >= b::State::Likely && th->source == "synrei");
    CHECK(a.primary == b::Kind::Thermal);

    // Same session data while Synrei reports boost: thermal is not a finding, CPU stays primary.
    auto g = synrei("boost");
    g.files = f.files;
    auto syn2 = adapter(g);
    m::RuntimeMetricsSampler s2(
        g, &display, {1000, 20},
        [&] { return std::optional<m::FpsObservation>(m::FpsObservation{fps_ts, 40.0, true, "game"}); },
        [&](int64_t now) { return syn2.read(now); });
    g.files["proc/stat"] = stat(100, 900);
    g.files[t::kSynreiStatePath] = state_file("boost", "96.0", "80.0", "44.0", wall);
    s2.start("s", 0);
    for (int i = 1; i <= 7; ++i) {
        g.files["proc/stat"] = stat(100 + i * 100, 900);
        g.files[t::kSynreiStatePath] = state_file("boost", "96.0", "80.0", "44.0", wall);
        fps_ts = i * 1000 - 100;
        s2.tick(i * 1000);
    }
    auto a2 = s2.assess(8000);
    CHECK(a2.get(b::Kind::Thermal)->state == b::State::Unknown); // measured, not throttling
    CHECK(a2.primary == b::Kind::Cpu);

    // No Synrei at all: thermal UNKNOWN, rules unchanged.
    FakeFs plain = f;
    plain.files.erase(t::kSynreiStatePath);
    auto syn3 = adapter(plain);
    m::RuntimeMetricsSampler s3(plain, nullptr, {1000, 20}, nullptr, [&](int64_t now) { return syn3.read(now); });
    s3.start("s", 0);
    s3.tick(1000);
    CHECK(s3.assess(2000).get(b::Kind::Thermal)->state == b::State::Unknown);
}

void test_failure_isolation() {
    auto f = synrei("safety");
    f.files["proc/stat"] = stat(100, 900);
    int calls = 0;
    m::RuntimeMetricsSampler sampler(f, nullptr, {1000, 10}, nullptr, [&](int64_t) -> t::ThermalSnapshot {
        ++calls;
        throw std::runtime_error("synrei adapter broke");
    });
    sampler.start("s", 0);
    for (int i = 1; i <= 5; ++i) {
        f.files["proc/stat"] = stat(100 + i * 50, 900 + i * 50);
        sampler.tick(i * 1000);
    }
    CHECK(sampler.running()); // thermal failure never stops sampling
    CHECK_EQ(sampler.samples_taken(), uint64_t(6));
    auto *c = sampler.last_snapshot()->get("thermal.constraint");
    CHECK(c && !c->readable && c->note.find("synrei adapter broke") != std::string::npos);
    CHECK(sampler.last_snapshot()->get("cpu.utilization.busiest_core")->readable);
    CHECK(sampler.assess(9000).get(b::Kind::Thermal)->state == b::State::Unknown);
    CHECK_EQ(calls, 6);

    // Adapter itself on a failing fs: UNKNOWN snapshot, no exception.
    auto bad = synrei("safety");
    bad.explode = true;
    auto s = adapter(bad).read(1);
    CHECK(!s.readable && s.constraint == t::Constraint::Unknown && !s.note.empty());

    // Synrei restart: new pid, fresh file -> verified again; slope history reset.
    auto r = synrei("boost");
    auto ad = adapter(r);
    ad.read(1000);
    r.files.erase("proc/4242/stat");
    r.files["proc/5151/stat"] = "5151 (hicod) S 1\n";
    r.files[t::kSynreiStatePath] = state_file("boost", "80.0", "70.0", "40.0", wall, 5151);
    auto after = ad.read(2000);
    CHECK(after.verified && !after.slope_c_per_min);
}

void test_zero_writes() {
    // Fake tree: identical after every read, and reads touch only Synrei's state file and /proc/<pid>.
    auto f = synrei("safety");
    f.files["sys/class/thermal/thermal_zone0/mode"] = "enabled\n";
    f.files["sys/class/thermal/thermal_zone0/trip_point_0_temp"] = "95000\n";
    auto before = f.files;
    auto a = adapter(f);
    for (int i = 0; i < 10; ++i) a.read(i * 1000);
    CHECK(f.files == before);
    for (auto &p : f.read_paths) CHECK(p == t::kSynreiStatePath || p.rfind("proc/", 0) == 0);

    // Real filesystem: the state file and a thermal node keep content and mtime.
    auto root = std::filesystem::temp_directory_path() / ("flux_synrei_" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "dev/hico");
    std::filesystem::create_directories(root / "proc/4242");
    std::filesystem::create_directories(root / "sys/class/thermal/thermal_zone0");
    { std::ofstream(root / "dev/hico/state") << state_file("safety", "70.0", "60.0", "35.0", wall); }
    { std::ofstream(root / "proc/4242/stat") << "4242 (hicod) S 1\n"; }
    { std::ofstream(root / "sys/class/thermal/thermal_zone0/mode") << "enabled\n"; }
    auto m1 = std::filesystem::last_write_time(root / "dev/hico/state");
    auto m2 = std::filesystem::last_write_time(root / "sys/class/thermal/thermal_zone0/mode");
    auto fs = k::make_readonly_fs(root.string());
    t::SynreiThermalAdapter real(*fs, [] { return wall; });
    auto s = real.read(1);
    CHECK(s.verified && s.constraint == t::Constraint::Constrained);
    CHECK(std::filesystem::last_write_time(root / "dev/hico/state") == m1);
    CHECK(std::filesystem::last_write_time(root / "sys/class/thermal/thermal_zone0/mode") == m2);
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_available();
    test_unavailable();
    test_missing_temperature();
    test_unreadable_source();
    test_stale();
    test_constraint_mapping();
    test_confidence_and_history();
    test_bottleneck_receives_thermal();
    test_failure_isolation();
    test_zero_writes();
    return flux_test::report("synrei_thermal_context_test");
}
