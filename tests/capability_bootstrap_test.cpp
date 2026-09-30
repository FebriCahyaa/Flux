// Runtime capability bootstrap (Step 7.6): success, failed probe, empty set, availability after
// "daemon start", repeated bootstrap, and the observer hook.
#include "flux_test.hpp"
#include "CapabilityBootstrap.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

namespace ctx = flux::context;
namespace k = flux::kernel;
namespace fs = std::filesystem;

namespace {

struct TempRoot {
    fs::path root;
    explicit TempRoot(const std::string &tag)
        : root(fs::temp_directory_path() / ("flux_boot_" + tag + std::to_string(::getpid()))) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempRoot() { fs::remove_all(root); }
    void put(const std::string &rel, const std::string &content) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << content;
    }
};

void device_tree(TempRoot &t) {
    t.put("proc/sys/kernel/osrelease", "5.10.198-android12-9-g1\n");
    t.put("sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq", "1804800\n");
    t.put("sys/devices/system/cpu/cpufreq/policy0/scaling_governor", "schedutil\n");
    t.put("sys/class/kgsl/kgsl-3d0/gpu_model", "Adreno650v2\n");
}

void test_successful_bootstrap() {
    TempRoot t("ok");
    device_tree(t);
    auto context = std::make_shared<ctx::CapabilityContext>();
    k::CapabilityBootstrap boot(context, k::make_device_probe(t.root.string(), [] {
        return k::PlatformHint{"kona", "qcom", "QTI"};
    }));
    CHECK(boot.last().status == k::BootstrapStatus::NotRun);
    auto r = boot.run();
    CHECK(r.status == k::BootstrapStatus::Ok);
    CHECK_EQ(r.attempt, 1);
    CHECK(r.facts > 0);
    CHECK(r.supported >= 3);
    CHECK(r.generation == 1);
    CHECK(r.error.empty());
    CHECK(context->supports("cpufreq.policy0.scaling_max_freq") == ctx::Support::Yes);
    auto gpu = context->resolve("gpu.kgsl.gpu_model");
    CHECK(gpu.fact && gpu.fact->source == "qualcomm" && gpu.fact->publisher == "kernel");
    auto integ = context->resolve("kernel.integration");
    CHECK(integ.fact && integ.fact->value == "gki");
    // Read-only: the tree is unchanged.
    std::ifstream in(t.root / "sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq");
    std::string v;
    std::getline(in, v);
    CHECK_EQ(v, std::string("1804800"));
}

void test_failed_probe() {
    auto context = std::make_shared<ctx::CapabilityContext>();
    k::CapabilityBootstrap boot(context, []() -> k::KernelReport { throw std::runtime_error("sysfs gone"); });
    k::BootstrapResult r;
    r = boot.run(); // must not throw
    CHECK(r.status == k::BootstrapStatus::Failed);
    CHECK_EQ(r.error, std::string("sysfs gone"));
    CHECK_EQ(r.facts, size_t(0));
    CHECK(context->publishers().empty());
    // Unavailable stays Unknown — nothing inferred.
    CHECK(context->supports("cpufreq.policy0.scaling_max_freq") == ctx::Support::Unknown);
    CHECK(context->supports("kernel.integration") == ctx::Support::Unknown);

    // Non-std exception and an empty probe function are failures too, never crashes.
    k::CapabilityBootstrap weird(context, []() -> k::KernelReport { throw 42; });
    CHECK(weird.run().status == k::BootstrapStatus::Failed);
    k::CapabilityBootstrap none(context, nullptr);
    CHECK(none.run().status == k::BootstrapStatus::Failed);
    k::CapabilityBootstrap no_context(nullptr, [] { return k::KernelReport{}; });
    CHECK(no_context.run().status == k::BootstrapStatus::Failed);
}

void test_empty_capability_set() {
    TempRoot t("empty"); // nothing under the root at all
    auto context = std::make_shared<ctx::CapabilityContext>();
    k::CapabilityBootstrap boot(context, k::make_device_probe(t.root.string(), nullptr));
    auto r = boot.run();
    CHECK(r.status == k::BootstrapStatus::Empty);
    CHECK_EQ(r.supported, size_t(0));
    CHECK(r.facts > 0); // absences are published as No, identity as Unknown
    for (const auto &id : context->ids())
        if (id.rfind("kernel.", 0) != 0) CHECK(context->supports(id) != ctx::Support::Yes);
    CHECK(context->resolve("kernel.adapter").fact->value == "generic");
    CHECK(context->supports("kernel.integration") == ctx::Support::Unknown);
    CHECK(context->supports("uclamp.top-app.min") == ctx::Support::No);
}

void test_context_available_after_start() {
    // What fluxd does: create the context, bootstrap, then hand the read-only view to engines.
    TempRoot t("avail");
    device_tree(t);
    auto context = std::make_shared<ctx::CapabilityContext>();
    k::CapabilityBootstrap boot(context, k::make_device_probe(t.root.string(), nullptr));
    std::shared_ptr<const ctx::CapabilityContext> engine_view = boot.context();
    CHECK(engine_view.get() == context.get());
    CHECK(engine_view->ids().empty()); // before start: nothing, everything Unknown
    boot.run();
    CHECK(!engine_view->ids().empty()); // same object now answers
    CHECK(engine_view->supports("governor.policy0") == ctx::Support::Yes);
    CHECK(engine_view->supports("never.published") == ctx::Support::Unknown);
}

void test_repeated_bootstrap() {
    TempRoot t("repeat");
    device_tree(t);
    auto context = std::make_shared<ctx::CapabilityContext>();
    bool fail = false;
    auto device = k::make_device_probe(t.root.string(), nullptr);
    k::CapabilityBootstrap boot(context, [&]() -> k::KernelReport {
        if (fail) throw std::runtime_error("transient");
        return device();
    });
    auto a = boot.run();
    auto ids_after_first = context->ids().size();
    auto b = boot.run();
    CHECK_EQ(b.attempt, 2);
    CHECK(b.status == k::BootstrapStatus::Ok);
    CHECK_EQ(context->ids().size(), ids_after_first); // snapshot replaced, no duplicates
    CHECK_EQ(context->facts("governor.policy0").size(), size_t(1));
    CHECK(b.generation == a.generation + 1);

    // A later failure keeps the last good snapshot and reports the failure.
    fail = true;
    auto c = boot.run();
    CHECK(c.status == k::BootstrapStatus::Failed);
    CHECK_EQ(c.attempt, 3);
    CHECK(c.generation == b.generation);
    CHECK(context->supports("governor.policy0") == ctx::Support::Yes);
    CHECK(boot.last().status == k::BootstrapStatus::Failed);

    // A changed device is reflected on the next run (removed node -> No).
    fail = false;
    fs::remove(t.root / "sys/devices/system/cpu/cpufreq/policy0/scaling_governor");
    boot.run();
    CHECK(context->supports("governor.policy0") == ctx::Support::No);
}

void test_observer_hook() {
    auto context = std::make_shared<ctx::CapabilityContext>();
    std::vector<k::BootstrapResult> seen;
    k::CapabilityBootstrap boot(context, []() -> k::KernelReport { throw std::runtime_error("x"); });
    boot.set_observer([&](const k::BootstrapResult &r) { seen.push_back(r); });
    boot.run();
    CHECK(seen.size() == 1 && seen[0].status == k::BootstrapStatus::Failed);
    boot.set_observer([](const k::BootstrapResult &) { throw 1; });
    CHECK(boot.run().status == k::BootstrapStatus::Failed); // observer failure swallowed
    CHECK_EQ(std::string(k::to_string(k::BootstrapStatus::Empty)), std::string("empty"));
}

} // namespace

int main() {
    test_successful_bootstrap();
    test_failed_probe();
    test_empty_capability_set();
    test_context_available_after_start();
    test_repeated_bootstrap();
    test_observer_hook();
    return flux_test::report("capability_bootstrap_test");
}
