// Graphics Intelligence (Step 8): GPU detection, missing GPU interfaces, Vulkan unavailable,
// conflicting GPU sources, and publishing into the capability context.
#include "flux_test.hpp"
#include "GraphicsIntelligence.hpp"

#include <map>
#include <set>

namespace ctx = flux::context;
namespace g = flux::graphics;
namespace k = flux::kernel;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
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
    bool writable_hint(const std::string &) const override { return true; } // must never surface
};

struct Props {
    std::map<std::string, std::string> v;
    std::function<std::string(const std::string &)> fn() {
        return [this](const std::string &k) { auto it = v.find(k); return it == v.end() ? std::string() : it->second; };
    }
};

using Facts = std::vector<ctx::CapabilityFact>;
const ctx::CapabilityFact *find(const Facts &f, const std::string &id) {
    for (auto &x : f)
        if (x.id == id) return &x;
    return nullptr;
}

flux::gfx::VulkanFacts adreno_vulkan() {
    flux::gfx::VulkanFacts v;
    v.available = true;
    v.status = "ok";
    v.loader_present = true;
    v.vendor_id = 0x5143;
    v.device_name = "Adreno (TM) 650";
    v.api_version_raw = (1u << 22) | (1u << 12) | 128; // 1.1.128
    v.driver_version_raw = 0x80000000u | 512;
    v.device_count = 1;
    return v;
}

FakeFs qualcomm_fs() {
    FakeFs f;
    f.files["sys/class/kgsl/kgsl-3d0/gpu_model"] = "Adreno650v2\n";
    f.files["vendor/lib64/hw/vulkan.adreno.so"] = "";
    return f;
}

ctx::CapabilityContext kernel_context(const FakeFs &f, const k::PlatformHint &hint) {
    ctx::CapabilityContext c;
    FakeFs copy = f;
    copy.files["sys/class/kgsl/kgsl-3d0/max_gpuclk"] = "587000000\n";
    copy.files["sys/class/kgsl/kgsl-3d0/gpubusy"] = "12 100\n";
    k::publish(k::observe(copy, hint, k::AdapterRegistry::with_builtin()), c);
    return c;
}

void test_gpu_detection() {
    Props p;
    p.v = {{"ro.hardware.egl", "adreno"}, {"ro.hardware.vulkan", "adreno"}, {"ro.opengles.version", "196610"}};
    auto fs = qualcomm_fs();
    auto kc = kernel_context(fs, {"kona", "qcom", "QTI"});
    auto facts = g::observe({p.fn(), &fs, adreno_vulkan(), &kc});

    auto *vendor = find(facts, "graphics.gpu.vendor");
    CHECK(vendor && vendor->value == "qualcomm" && vendor->support == ctx::Support::Yes);
    CHECK(vendor && vendor->confidence == ctx::Confidence::High);
    CHECK(vendor && vendor->note.find("conflict") == std::string::npos);
    auto *model = find(facts, "graphics.gpu.model");
    CHECK(model && model->value == "Adreno (TM) 650" && model->source == "vulkan");
    auto *vk = find(facts, "graphics.vulkan.available");
    CHECK(vk && vk->support == ctx::Support::Yes && vk->confidence == ctx::Confidence::High);
    auto *api = find(facts, "graphics.vulkan.api_version");
    CHECK(api && api->value == "1.1.128");
    auto *drv = find(facts, "graphics.driver.vulkan_version");
    CHECK(drv && drv->support == ctx::Support::Yes && !drv->value.empty());
    auto *gles = find(facts, "graphics.opengles.version");
    CHECK(gles && gles->value == "3.2" && gles->confidence == ctx::Confidence::Medium);
    auto *egl = find(facts, "graphics.egl.driver");
    CHECK(egl && egl->value == "adreno");
    auto *ifs = find(facts, "graphics.interfaces");
    CHECK(ifs && ifs->value == "vulkan opengles egl");
    auto *freq = find(facts, "graphics.gpu.freq_interface");
    CHECK(freq && freq->support == ctx::Support::Yes && freq->value.find("gpu.kgsl.max_gpuclk") != std::string::npos);
    auto *load = find(facts, "graphics.gpu.load_interface");
    CHECK(load && load->support == ctx::Support::Yes && load->value.find("gpu.kgsl.gpubusy") != std::string::npos);

    for (auto &f : facts) {
        CHECK_EQ(f.domain, std::string("graphics"));
        CHECK(!f.writable); // capability only; never a write target
        CHECK(!f.verified);
        CHECK(f.id.rfind("graphics.", 0) == 0);
    }
}

void test_missing_gpu_interface() {
    // No properties, no sysfs, no Vulkan probe, no kernel facts: everything Unknown, nothing guessed.
    FakeFs empty;
    auto facts = g::observe({nullptr, &empty, std::nullopt, nullptr});
    for (auto id : {"graphics.gpu.vendor", "graphics.gpu.model", "graphics.vulkan.available",
                    "graphics.opengles.version", "graphics.gpu.freq_interface", "graphics.gpu.load_interface",
                    "graphics.interfaces", "graphics.driver.vulkan_version"}) {
        auto *f = find(facts, id);
        CHECK(f != nullptr);
        if (!f) continue;
        CHECK(f->support == ctx::Support::Unknown);
        CHECK(f->confidence == ctx::Confidence::None);
        CHECK(f->value.empty());
        CHECK(!f->note.empty());
    }
    // Kernel facts present but no GPU node supported -> the interface is No (observed absence).
    ctx::CapabilityContext kc;
    k::publish(k::observe(empty, {}, k::AdapterRegistry::with_builtin()), kc);
    auto f2 = g::observe({nullptr, &empty, std::nullopt, &kc});
    auto *freq = find(f2, "graphics.gpu.freq_interface");
    CHECK(freq && freq->support == ctx::Support::No && freq->confidence == ctx::Confidence::Medium);
    auto *load = find(f2, "graphics.gpu.load_interface");
    CHECK(load && load->support == ctx::Support::No);
    // Null evidence entirely must not crash.
    auto f3 = g::observe({});
    CHECK(find(f3, "graphics.gpu.vendor") != nullptr);
}

void test_vulkan_unavailable() {
    Props p;
    p.v = {{"ro.hardware.egl", "mali"}, {"ro.opengles.version", "196610"}};
    FakeFs fs;
    flux::gfx::VulkanFacts none;
    none.available = false;
    none.status = "absent";
    none.loader_present = false;
    auto facts = g::observe({p.fn(), &fs, none, nullptr});
    auto *vk = find(facts, "graphics.vulkan.available");
    CHECK(vk && vk->support == ctx::Support::No && vk->confidence == ctx::Confidence::High);
    CHECK(vk && vk->note.find("absent") != std::string::npos);
    CHECK(find(facts, "graphics.vulkan.api_version")->support == ctx::Support::Unknown);
    auto *ifs = find(facts, "graphics.interfaces");
    CHECK(ifs && ifs->value == "opengles egl");

    flux::gfx::VulkanFacts nodev;
    nodev.status = "no_device";
    nodev.loader_present = true;
    auto f2 = g::observe({p.fn(), &fs, nodev, nullptr});
    CHECK(find(f2, "graphics.vulkan.available")->support == ctx::Support::No);

    // Declarative only (no instance probe): driver file + property -> Yes, but Medium.
    Props q;
    q.v = {{"ro.hardware.vulkan", "mali"}};
    FakeFs lib;
    lib.files["vendor/lib64/hw/vulkan.mali.so"] = "";
    auto f3 = g::observe({q.fn(), &lib, std::nullopt, nullptr});
    auto *d = find(f3, "graphics.vulkan.available");
    CHECK(d && d->support == ctx::Support::Yes && d->confidence == ctx::Confidence::Medium);
    // Property without driver file is weaker still; nothing at all stays Unknown (never No).
    auto f4 = g::observe({q.fn(), &fs, std::nullopt, nullptr});
    CHECK(find(f4, "graphics.vulkan.available")->confidence == ctx::Confidence::Low);
    auto f5 = g::observe({nullptr, &fs, std::nullopt, nullptr});
    CHECK(find(f5, "graphics.vulkan.available")->support == ctx::Support::Unknown);
}

void test_conflicting_gpu_sources() {
    // Vulkan reports ARM, sysfs reports an Adreno: equal (High) evidence disagrees -> Unknown.
    auto vk = adreno_vulkan();
    vk.vendor_id = 0x13B5;
    vk.device_name = "Mali-G77";
    auto fs = qualcomm_fs();
    Props p;
    p.v = {{"ro.hardware.egl", "adreno"}};
    auto facts = g::observe({p.fn(), &fs, vk, nullptr});
    auto *vendor = find(facts, "graphics.gpu.vendor");
    CHECK(vendor && vendor->support == ctx::Support::Unknown);
    CHECK(vendor && vendor->note.find("conflict") != std::string::npos);
    CHECK(vendor && vendor->value.empty());
    // Per-source facts are kept.
    CHECK(find(facts, "graphics.gpu.vendor.vulkan") && find(facts, "graphics.gpu.vendor.vulkan")->value == "arm");
    CHECK(find(facts, "graphics.gpu.vendor.sysfs") && find(facts, "graphics.gpu.vendor.sysfs")->value == "qualcomm");
    CHECK(find(facts, "graphics.gpu.vendor.egl") && find(facts, "graphics.gpu.vendor.egl")->value == "qualcomm");
    auto *model = find(facts, "graphics.gpu.model");
    CHECK(model && model->support == ctx::Support::Unknown);

    // Weaker disagreement (property only): the stronger source answers, conflict noted.
    FakeFs none;
    Props mali;
    mali.v = {{"ro.hardware.egl", "mali"}};
    auto f2 = g::observe({mali.fn(), &none, adreno_vulkan(), nullptr});
    auto *v2 = find(f2, "graphics.gpu.vendor");
    CHECK(v2 && v2->value == "qualcomm" && v2->support == ctx::Support::Yes);
    CHECK(v2 && v2->note.find("conflict") != std::string::npos);

    // Unrecognised EGL name is not evidence for any vendor.
    Props odd;
    odd.v = {{"ro.hardware.egl", "meow"}};
    auto f3 = g::observe({odd.fn(), &none, std::nullopt, nullptr});
    CHECK(find(f3, "graphics.gpu.vendor")->support == ctx::Support::Unknown);
    CHECK(find(f3, "graphics.egl.driver")->value == "meow");
}

void test_capability_publishing() {
    Props p;
    p.v = {{"ro.hardware.egl", "adreno"}, {"ro.opengles.version", "196610"}};
    auto fs = qualcomm_fs();
    ctx::CapabilityContext c = kernel_context(fs, {"kona", "qcom", "QTI"});
    auto before = c.ids().size();
    g::publish({p.fn(), &fs, adreno_vulkan(), &c}, c);
    CHECK(c.ids().size() > before);
    CHECK_EQ(c.publishers().size(), size_t(2)); // kernel + graphics
    auto vendor = c.resolve("graphics.gpu.vendor");
    CHECK(vendor.support == ctx::Support::Yes);
    CHECK(vendor.fact && vendor.fact->publisher == "graphics" && vendor.fact->value == "qualcomm");
    CHECK(!c.ids("graphics").empty());
    for (auto &id : c.ids("graphics")) CHECK(id.rfind("graphics.", 0) == 0);
    // Kernel facts are untouched by the graphics publish.
    CHECK(c.supports("gpu.kgsl.max_gpuclk") == ctx::Support::Yes);
    // Republish replaces the graphics snapshot, no duplicates.
    auto n = c.ids().size();
    g::publish({p.fn(), &fs, adreno_vulkan(), &c}, c);
    CHECK_EQ(c.ids().size(), n);
    CHECK_EQ(c.facts("graphics.gpu.vendor").size(), size_t(1));
    CHECK_EQ(std::string(ctx::to_string(ctx::Confidence::None)), std::string("unknown"));
}

void test_gles_format() {
    CHECK_EQ(g::format_gles_version("196610"), std::string("3.2"));
    CHECK_EQ(g::format_gles_version("131072"), std::string("2.0"));
    CHECK_EQ(g::format_gles_version(""), std::string(""));
    CHECK_EQ(g::format_gles_version("abc"), std::string(""));
    CHECK_EQ(g::format_gles_version("0"), std::string(""));
}

} // namespace

int main() {
    test_gpu_detection();
    test_missing_gpu_interface();
    test_vulkan_unavailable();
    test_conflicting_gpu_sources();
    test_capability_publishing();
    test_gles_format();
    return flux_test::report("graphics_intelligence_test");
}
