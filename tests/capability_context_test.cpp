// Capability context bridge (Step 7.5): kernel export, confidence preservation, missing and
// unknown capabilities, conflicting publishers, and the planner receiving the context.
#include "flux_test.hpp"
#include "CapabilityContext.hpp"
#include "PerformancePlanner.hpp"
#include "kernel/KernelIntelligence.hpp"

#include <map>
#include <memory>
#include <set>

namespace ctx = flux::context;
namespace k = flux::kernel;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
    std::set<std::string> writable;
    Kind kind(const std::string &p) const override {
        if (files.count(p)) return Kind::File;
        auto it = files.lower_bound(p + "/");
        return it != files.end() && it->first.rfind(p + "/", 0) == 0 ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &p) const override {
        auto it = files.find(p);
        return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
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
    bool writable_hint(const std::string &p) const override { return writable.count(p) > 0; }
};

const std::string kMax = "sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq";

k::KernelReport qualcomm_report() {
    FakeFs f;
    f.files["proc/sys/kernel/osrelease"] = "5.10.198-android12-9-g1\n";
    f.files[kMax] = "1804800\n";
    f.files["sys/devices/system/cpu/cpufreq/policy0/scaling_governor"] = "bogus\x01\n"; // invalid
    f.files["sys/block/sda/queue/scheduler"] = "[mq-deadline] none\n";
    f.files["sys/class/thermal/thermal_zone0/temp"] = "40000\n";
    f.files["sys/class/kgsl/kgsl-3d0/gpu_model"] = "Adreno650v2\n";
    f.writable = {kMax, "sys/class/thermal/thermal_zone0/temp"};
    return k::observe(f, {"kona", "qcom", "QTI"}, k::AdapterRegistry::with_builtin());
}

void test_kernel_capability_exported() {
    auto report = qualcomm_report();
    ctx::CapabilityContext c;
    k::publish(report, c);
    CHECK_EQ(c.publishers().size(), size_t(1));
    CHECK(c.generation() == 1);
    CHECK_EQ(c.ids().size(), report.capabilities.size() + 3);

    auto r = c.resolve("cpufreq.policy0.scaling_max_freq");
    CHECK(r.support == ctx::Support::Yes);
    CHECK(r.fact != nullptr);
    if (r.fact) {
        CHECK_EQ(r.fact->publisher, std::string("kernel"));
        CHECK_EQ(r.fact->source, std::string("generic"));
        CHECK_EQ(r.fact->domain, std::string("cpufreq"));
        CHECK_EQ(r.fact->value, std::string("1804800"));
        CHECK_EQ(r.fact->interface, kMax);
        CHECK(r.fact->readable);
        CHECK(r.fact->writable);
        CHECK(!r.fact->verified);
        CHECK(r.fact->risk == ctx::Risk::Medium);
        CHECK(r.fact->rollback);
    }
    auto gpu = c.resolve("gpu.kgsl.gpu_model");
    CHECK(gpu.fact && gpu.fact->source == "qualcomm" && gpu.fact->requires_adapter);
    auto adapter = c.resolve("kernel.adapter");
    CHECK(adapter.fact && adapter.fact->value == "qualcomm");
    auto integ = c.resolve("kernel.integration");
    CHECK(integ.support == ctx::Support::Yes);
    CHECK(integ.fact && integ.fact->value == "gki");
    CHECK(c.ids("gpu").size() >= 1);
}

void test_confidence_preserved() {
    auto report = qualcomm_report();
    auto facts = k::export_facts(report);
    CHECK_EQ(facts.size(), report.capabilities.size() + 3);
    for (size_t i = 0; i < report.capabilities.size(); ++i) {
        const auto &kc = report.capabilities[i];
        const auto &f = facts[i];
        CHECK_EQ(f.id, kc.id);
        CHECK_EQ(static_cast<int>(f.confidence), static_cast<int>(kc.confidence));
        CHECK_EQ(f.readable, kc.readable);
        CHECK_EQ(f.writable, kc.writable);
        CHECK_EQ(f.verified, kc.verified);
        CHECK_EQ(f.source, kc.source);
        CHECK_EQ(std::string(ctx::to_string(f.risk)), std::string(k::to_string(kc.risk)));
    }
    // Observe-only thermal stays non-writable and High risk through the bridge.
    ctx::CapabilityContext c;
    c.publish(k::kContextPublisher, facts);
    auto t = c.resolve("thermal.thermal_zone0.temp");
    CHECK(t.fact && !t.fact->writable && t.fact->risk == ctx::Risk::High);
    // Invalid node: supported, unreadable, Low confidence — not upgraded.
    auto g = c.resolve("governor.policy0");
    CHECK(g.support == ctx::Support::Yes);
    CHECK(g.fact && !g.fact->readable && g.fact->confidence == ctx::Confidence::Low);
    auto integ = c.resolve("kernel.integration");
    CHECK(integ.fact && integ.fact->confidence == ctx::Confidence::High);
}

void test_missing_and_unknown() {
    ctx::CapabilityContext c;
    // Nothing published: unknown, not "no".
    auto none = c.resolve("cpufreq.policy0.scaling_max_freq");
    CHECK(none.support == ctx::Support::Unknown);
    CHECK(none.fact == nullptr);
    CHECK(none.publishers.empty());

    FakeFs empty;
    auto report = k::observe(empty, {}, k::AdapterRegistry::with_builtin());
    k::publish(report, c);
    // Observed absence is No (with the prober's confidence), never Yes.
    auto miss = c.resolve("uclamp.top-app.min");
    CHECK(miss.support == ctx::Support::No);
    CHECK(miss.fact && !miss.fact->readable && !miss.fact->writable && miss.fact->confidence == ctx::Confidence::Medium);
    // Unclassifiable kernel stays Unknown.
    auto integ = c.resolve("kernel.integration");
    CHECK(integ.support == ctx::Support::Unknown);
    CHECK(integ.fact == nullptr); // Unknown has no deciding fact
    auto raw = c.facts("kernel.integration");
    CHECK(raw.size() == 1 && raw[0]->value == "unknown" && raw[0]->confidence == ctx::Confidence::None);
    // An id no producer knows is Unknown even with a populated context.
    CHECK(c.supports("gpu.vulkan.ray_query") == ctx::Support::Unknown);
    // Unknown facts never win against nothing: a lone Unknown fact resolves Unknown.
    ctx::CapabilityFact rq;
    rq.id = "gpu.vulkan.ray_query";
    rq.domain = "gpu";
    rq.source = "vulkan";
    c.publish("graphics", {rq});
    CHECK(c.supports("gpu.vulkan.ray_query") == ctx::Support::Unknown);
}

ctx::CapabilityFact fact(const std::string &id, ctx::Support s, ctx::Confidence conf, bool writable = false) {
    ctx::CapabilityFact f;
    f.id = id;
    f.domain = "io_scheduler";
    f.source = "test";
    f.support = s;
    f.confidence = conf;
    f.readable = s == ctx::Support::Yes;
    f.writable = writable;
    return f;
}

void test_conflicting_sources() {
    ctx::CapabilityContext c;
    c.publish("kernel", {fact("io.sda.scheduler", ctx::Support::Yes, ctx::Confidence::High, true)});
    c.publish("aeyrin", {fact("io.sda.scheduler", ctx::Support::No, ctx::Confidence::High)});
    auto r = c.resolve("io.sda.scheduler");
    CHECK(r.support == ctx::Support::Unknown); // equal confidence, different answers
    CHECK(r.conflict);
    CHECK(r.fact == nullptr);
    CHECK_EQ(r.publishers.size(), size_t(2));
    CHECK_EQ(c.facts("io.sda.scheduler").size(), size_t(2)); // both kept

    // Weaker disagreement: the stronger fact answers, the conflict stays visible.
    c.publish("aeyrin", {fact("io.sda.scheduler", ctx::Support::No, ctx::Confidence::Low)});
    auto r2 = c.resolve("io.sda.scheduler");
    CHECK(r2.support == ctx::Support::Yes);
    CHECK(r2.conflict);
    CHECK(r2.fact && r2.fact->publisher == "kernel" && r2.fact->writable);

    // Fields are not merged: a writable=true from a weaker source does not leak.
    c.publish("aeyrin", {fact("x", ctx::Support::Yes, ctx::Confidence::Low, true)});
    c.publish("kernel", {fact("x", ctx::Support::Yes, ctx::Confidence::High, false)});
    auto r3 = c.resolve("x");
    CHECK(r3.fact && !r3.fact->writable && !r3.conflict);
    // Republishing replaces that publisher's snapshot: io.sda.scheduler from kernel is gone.
    CHECK_EQ(c.facts("io.sda.scheduler").size(), size_t(0));
}

void test_observer_interface() {
    ctx::CapabilityContext c;
    std::vector<ctx::ContextNotice> seen;
    c.set_observer([&](const ctx::ContextNotice &n) { seen.push_back(n); });
    c.publish("kernel", {fact("a", ctx::Support::Yes, ctx::Confidence::High),
                         fact("b", ctx::Support::No, ctx::Confidence::Medium),
                         fact("c", ctx::Support::Unknown, ctx::Confidence::None),
                         fact("", ctx::Support::Yes, ctx::Confidence::High)}); // dropped
    CHECK_EQ(seen.size(), size_t(1));
    if (!seen.empty()) {
        CHECK_EQ(seen[0].publisher, std::string("kernel"));
        CHECK_EQ(seen[0].facts, 3);
        CHECK_EQ(seen[0].supported, 1);
        CHECK_EQ(seen[0].unsupported, 1);
        CHECK_EQ(seen[0].unknown, 1);
        CHECK(seen[0].generation == 1);
    }
    c.publish("aeyrin", {fact("a", ctx::Support::No, ctx::Confidence::High)});
    CHECK(seen.size() == 2 && seen[1].conflicts == 1);
    // A throwing observer never breaks publishing.
    c.set_observer([](const ctx::ContextNotice &) { throw 1; });
    c.publish("kernel", {fact("z", ctx::Support::Yes, ctx::Confidence::High)});
    CHECK(c.supports("z") == ctx::Support::Yes);
}

void test_planner_receives_context() {
    auto context = std::make_shared<ctx::CapabilityContext>();
    k::publish(qualcomm_report(), *context);

    std::map<std::string, std::string> nodes = {{"/proc/sys/vm/swappiness", "100"}};
    flux::runtime::Io io;
    io.exists = [&](const std::string &p) { return nodes.count(p) > 0; };
    io.read = [&](const std::string &p) -> std::optional<std::string> {
        auto it = nodes.find(p);
        return it == nodes.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    io.write = [](const std::string &, const std::string &) { return false; };
    auto caps = [&](std::shared_ptr<const ctx::CapabilityContext> cx) {
        flux::perf::PerfCapabilities pc;
        pc.node_available = [&](const std::string &p) { return nodes.count(p) > 0; };
        pc.context = std::move(cx);
        return pc;
    };
    flux::perf::PerformancePlanner with(io, caps(context));
    flux::perf::PerformancePlanner without(io, caps(nullptr));
    CHECK(with.capability_context() == context.get());
    CHECK(without.capability_context() == nullptr);
    if (auto *cx = with.capability_context())
        CHECK(cx->supports("cpufreq.policy0.scaling_max_freq") == ctx::Support::Yes);

    // Receiving the context changes no decision (no policy in this step).
    flux::perf::PerfProfile p;
    p.memory = "gaming";
    auto a = with.plan(p, {"com.example.game"});
    auto b = without.plan(p, {"com.example.game"});
    CHECK_EQ(a.plan.operations.size(), b.plan.operations.size());
    CHECK_EQ(a.errors.size(), b.errors.size());
    CHECK_EQ(a.refresh_target_hz, b.refresh_target_hz);
}

} // namespace

int main() {
    test_kernel_capability_exported();
    test_confidence_preserved();
    test_missing_and_unknown();
    test_conflicting_sources();
    test_observer_interface();
    test_planner_receives_context();
    return flux_test::report("capability_context_test");
}
