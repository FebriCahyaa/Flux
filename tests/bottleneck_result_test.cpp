// Bottleneck result integration (Step 8.10): results per category, conflict, insufficient
// evidence, event emission (once, at session end, after the assessment), failure isolation.
#include "flux_test.hpp"
#include "BottleneckEvents.hpp"
#include "EventStore.hpp"
#include "RuntimeMetricsSampler.hpp"

#include <set>
#include <stdexcept>

namespace b = flux::bottleneck;
namespace ctx = flux::context;
namespace k = flux::kernel;
namespace m = flux::metrics;
namespace o = flux::observatory;
namespace s = flux::session;

namespace {

constexpr int64_t kWall = 1'760'000'000'000; // 2025-10

b::RuntimeSample sample(int i, double fps = 40, double target = 60) {
    b::RuntimeSample x;
    x.timestamp_ms = 1000 + i * 1000;
    x.fps = fps;
    x.target_hz = target;
    return x;
}

b::Assessment run(void (*fill)(b::RuntimeSample &), const b::ThermalContext *thermal = nullptr,
                  const ctx::CapabilityContext *c = nullptr, double fps = 40) {
    b::BottleneckInputs in;
    for (int i = 0; i < 6; ++i) {
        auto x = sample(i, fps);
        fill(x);
        in.session.samples.push_back(x);
    }
    in.thermal = thermal;
    in.context = c;
    in.now_ms = 9000;
    return b::assess(in);
}

void expect_primary(const b::BottleneckResult &r, b::Kind k, b::State st) {
    CHECK(r.primary.kind == k);
    CHECK(r.primary.rating == st);
    CHECK(!r.primary.evidence.empty());
    CHECK(!r.primary.source.empty());
    CHECK(r.primary.timestamp_ms > 0);
    CHECK(r.primary.confidence != ctx::Confidence::None);
}

void test_cpu_gpu() {
    auto cpu = b::make_result(run([](b::RuntimeSample &x) {
                                  x.cpu_busiest_core = 0.98;
                                  x.cpu_freq_ratio = 1.0;
                                  x.gpu_busy = 0.4;
                              }),
                              "s-1");
    expect_primary(cpu, b::Kind::Cpu, b::State::Confirmed);
    CHECK(cpu.primary.confidence == ctx::Confidence::High);
    CHECK(!cpu.conflict && cpu.session_id == "s-1" && cpu.samples == 6);
    auto gpu = b::make_result(run([](b::RuntimeSample &x) {
                                  x.gpu_busy = 0.97;
                                  x.gpu_freq_ratio = 1.0;
                                  x.cpu_busiest_core = 0.4;
                              }),
                              "s-2");
    expect_primary(gpu, b::Kind::Gpu, b::State::Confirmed);
}

struct Hot : b::ThermalContext {
    std::optional<b::ThermalState> at(int64_t t) const override { return b::ThermalState{t, true, std::nullopt, "synrei"}; }
};

void test_thermal_memory_storage() {
    Hot hot;
    auto th = b::make_result(run([](b::RuntimeSample &x) {
                                 x.cpu_busiest_core = 0.98;
                                 x.cpu_freq_ratio = 1.0;
                             },
                                 &hot),
                             "s");
    expect_primary(th, b::Kind::Thermal, b::State::Confirmed);
    CHECK(th.primary.source == "synrei");
    bool cpu_secondary = false;
    for (auto &f : th.secondary)
        if (f.kind == b::Kind::Cpu) cpu_secondary = f.rating >= b::State::Likely;
    CHECK(cpu_secondary); // CPU saturation kept as a secondary finding

    auto mem = b::make_result(run([](b::RuntimeSample &x) {
                                  x.mem_psi_some = 30;
                                  x.cpu_busiest_core = 0.3;
                                  x.gpu_busy = 0.3;
                              }),
                              "s");
    expect_primary(mem, b::Kind::Memory, b::State::Confirmed);
    auto io = b::make_result(run([](b::RuntimeSample &x) {
                                 x.io_psi_some = 40;
                                 x.cpu_busiest_core = 0.3;
                                 x.gpu_busy = 0.3;
                             }),
                             "s");
    expect_primary(io, b::Kind::Storage, b::State::Confirmed);
}

void test_display() {
    ctx::CapabilityContext c;
    auto f = [](const std::string &id, const std::string &v) {
        ctx::CapabilityFact x;
        x.id = id;
        x.domain = "display";
        x.support = ctx::Support::Yes;
        x.readable = true;
        x.confidence = ctx::Confidence::High;
        x.value = v;
        return x;
    };
    c.publish("display", {f("display.refresh.current_hz", "60"), f("display.refresh.max_hz", "120")});
    auto d = b::make_result(run([](b::RuntimeSample &x) {
                                x.cpu_busiest_core = 0.4;
                                x.gpu_busy = 0.4;
                            },
                                nullptr, &c, 59.9),
                            "s");
    expect_primary(d, b::Kind::Display, b::State::Confirmed);
}

void test_conflict_and_insufficient() {
    auto both = b::make_result(run([](b::RuntimeSample &x) {
                                   x.cpu_busiest_core = 0.97;
                                   x.cpu_freq_ratio = 1.0;
                                   x.gpu_busy = 0.97;
                               }),
                               "s");
    CHECK(both.conflict);
    CHECK(both.primary.kind == b::Kind::Unknown && both.primary.rating == b::State::Unknown);
    CHECK(both.primary.evidence.empty());
    std::set<b::Kind> kinds;
    for (auto &f : both.secondary) kinds.insert(f.kind);
    CHECK(kinds.count(b::Kind::Cpu) && kinds.count(b::Kind::Gpu));
    CHECK(both.note.find("conflict") != std::string::npos);

    b::BottleneckInputs none;
    none.now_ms = 5000;
    auto empty = b::make_result(b::assess(none), "s");
    CHECK(empty.primary.kind == b::Kind::Unknown);
    CHECK(empty.primary.confidence == ctx::Confidence::None);
    CHECK(empty.secondary.empty());
    CHECK(!empty.conflict);
    CHECK(empty.note.find("insufficient") != std::string::npos);
}

void test_event_contents() {
    auto r = b::make_result(run([](b::RuntimeSample &x) {
                                x.cpu_busiest_core = 0.98;
                                x.cpu_freq_ratio = 1.0;
                                x.gpu_busy = 0.4;
                            }),
                            "s-9");
    auto e = flux::bridge::bottleneck_event(r, kWall);
    auto reg = o::EventRegistry::builtin();
    CHECK(reg.find("BOTTLENECK_ASSESSED") && reg.find("BOTTLENECK_ANALYSIS_FAILED"));
    CHECK(o::validate(e, reg, kWall).empty());
    CHECK_EQ(e.type, std::string("BOTTLENECK_ASSESSED"));
    CHECK_EQ(e.source, std::string("bottleneck"));
    CHECK_EQ(e.session_id, std::string("s-9"));
    CHECK(e.result == o::Result::Ok);
    CHECK(e.confidence == o::Confidence::High);
    CHECK_EQ(e.after.at("primary"), std::string("cpu"));
    CHECK_EQ(e.after.at("rating"), std::string("confirmed"));
    CHECK_EQ(e.after.at("conflict"), std::string("false"));
    CHECK_EQ(e.after.at("samples"), std::string("6"));
    CHECK(e.after.count("evidence.cpu.0") && e.after.at("evidence.cpu.0").find("cpu_busiest_core") != std::string::npos);
    CHECK(e.after.count("evidence.cpu.1")); // exclusion / deficit / performance state also present
    size_t ev = 0;
    for (auto &[key, _] : e.after)
        if (key.rfind("evidence.cpu.", 0) == 0) ++ev;
    CHECK(ev <= flux::bridge::kMaxEvidencePerFinding);
    CHECK(e.reason.find("cpu") != std::string::npos);
    std::string err;
    o::Event back;
    CHECK(o::from_json(o::to_json(e), back, err)); // serialisable
    CHECK(back.after == e.after);

    // Insufficient evidence still produces a valid, honest event.
    b::BottleneckInputs none;
    none.now_ms = 1;
    auto u = flux::bridge::bottleneck_event(b::make_result(b::assess(none), "s"), kWall);
    CHECK(o::validate(u, reg, kWall).empty());
    CHECK_EQ(u.after.at("primary"), std::string("unknown"));
    CHECK(u.confidence == o::Confidence::Unknown);
    CHECK(u.reason.find("insufficient") != std::string::npos);

    auto fail = flux::bridge::bottleneck_failure_event("s", "analyzer exploded", kWall);
    CHECK(o::validate(fail, reg, kWall).empty());
    CHECK(fail.result == o::Result::Failed && fail.severity == o::Severity::Warning);
    CHECK(fail.reason.find("analyzer exploded") != std::string::npos);
}

// -- session lifecycle ---------------------------------------------------------------------

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

/// Stands in for the game runtime participant: records that its restore ran.
struct RuntimeStandIn : s::SessionParticipant {
    int restores = 0;
    std::string tx_state = "active";
    const char *name() const override { return "game_runtime"; }
    void begin(const s::SessionInfo &) override { tx_state = "active"; }
    bool end(const s::SessionInfo &, s::EndReason) override {
        ++restores;
        tx_state = "restored";
        return true;
    }
};

struct Harness {
    FakeFs fs;
    m::RuntimeMetricsSampler sampler{fs, nullptr, {1000, 20}};
    m::SamplerParticipant part{sampler};
    RuntimeStandIn runtime;
    s::SessionManager mgr;
    o::MemoryEventStore store{o::EventRegistry::builtin(), [] { return kWall; }};
    int results = 0, failures = 0;
    Harness() {
        fs.files["proc/stat"] = "cpu  1 0 0 9 0 0 0 0\ncpu0 1 0 0 9 0 0 0 0\n";
        mgr.add(&runtime);
        mgr.add(&part); // last: ends first, like fluxd
        mgr.recover();
        sampler.set_result_sink(
            [this](const b::BottleneckResult &r) {
                ++results;
                store.write(flux::bridge::bottleneck_event(r, kWall));
            },
            [this](const std::string &sid, const std::string &err, int64_t) {
                ++failures;
                store.write(flux::bridge::bottleneck_failure_event(sid, err, kWall));
            });
    }
    std::vector<o::Event> events(const std::string &type) const {
        o::EventQuery q;
        q.type = type;
        return store.query(q);
    }
};

s::SessionKey key(const std::string &p, int pid) {
    s::SessionKey k;
    k.package = p;
    k.pid = pid;
    return k;
}

void test_emitted_once_at_session_end() {
    Harness h;
    h.mgr.begin(key("com.game", 1), 1000);
    for (int i = 2; i <= 8; ++i) h.mgr.tick(i * 1000);
    CHECK_EQ(h.results, 0); // nothing while the session runs
    CHECK(h.events("BOTTLENECK_ASSESSED").empty());
    CHECK(h.mgr.end(s::EndReason::Exit, 9000));
    CHECK_EQ(h.results, 1);
    auto ev = h.events("BOTTLENECK_ASSESSED");
    CHECK_EQ(ev.size(), size_t(1));
    if (!ev.empty()) {
        CHECK_EQ(ev[0].session_id, h.mgr.last().id);
        CHECK_EQ(ev[0].after.at("samples"), std::to_string(h.sampler.final_assessment()->samples));
        CHECK_EQ(ev[0].after.at("primary"), std::string("unknown")); // fps unknown here: no invented finding
    }
    // Duplicate end and later ticks: no second result.
    CHECK(!h.mgr.end(s::EndReason::Exit, 9500));
    h.mgr.tick(20000);
    CHECK_EQ(h.results, 1);
    // Switch: the replaced session reports once, the new one at its own end.
    h.mgr.begin(key("com.a", 2), 30000);
    h.mgr.begin(key("com.b", 3), 31000);
    CHECK_EQ(h.results, 2);
    h.mgr.end(s::EndReason::DaemonStop, 32000);
    CHECK_EQ(h.results, 3);
    CHECK_EQ(h.events("BOTTLENECK_ASSESSED").size(), size_t(3));
    CHECK(h.sampler.last_result().has_value());
}

void test_failure_isolation() {
    Harness h;
    h.sampler.set_analyzer([](const b::BottleneckInputs &) -> b::Assessment { throw std::runtime_error("analyzer exploded"); });
    h.mgr.begin(key("com.game", 1), 1000);
    h.mgr.tick(2000);
    CHECK(h.mgr.end(s::EndReason::ProcessDeath, 3000)); // session ends cleanly
    CHECK_EQ(h.runtime.restores, 1);                     // restore still ran
    CHECK_EQ(h.runtime.tx_state, std::string("restored"));
    CHECK_EQ(h.results, 0);
    CHECK_EQ(h.failures, 1);
    auto ev = h.events("BOTTLENECK_ANALYSIS_FAILED");
    CHECK(ev.size() == 1 && ev[0].reason.find("analyzer exploded") != std::string::npos);
    CHECK(h.events("BOTTLENECK_ASSESSED").empty()); // no success claimed

    // Throwing sinks (Observatory problems) are swallowed as well.
    Harness g;
    g.sampler.set_result_sink([](const b::BottleneckResult &) { throw 1; },
                              [](const std::string &, const std::string &, int64_t) { throw 2; });
    g.mgr.begin(key("com.game", 1), 1000);
    CHECK(g.mgr.end(s::EndReason::Exit, 2000));
    CHECK_EQ(g.runtime.restores, 1);
    g.sampler.set_analyzer([](const b::BottleneckInputs &) -> b::Assessment { throw 3; });
    g.mgr.begin(key("com.game", 2), 3000);
    CHECK(g.mgr.end(s::EndReason::Exit, 4000));
    CHECK_EQ(g.runtime.restores, 2);
}

} // namespace

int main() {
    test_cpu_gpu();
    test_thermal_memory_storage();
    test_display();
    test_conflict_and_insufficient();
    test_event_contents();
    test_emitted_once_at_session_end();
    test_failure_isolation();
    return flux_test::report("bottleneck_result_test");
}
