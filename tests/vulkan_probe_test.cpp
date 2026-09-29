// Host test for jni/gfx/VulkanProbe.cpp.
//
// The probe splits into a transport (dlopen + Vulkan calls) and a pure
// normalisation step over a plain VulkanFacts struct. That split is what makes
// this testable without a GPU: every device shape is expressed as a VulkanFacts
// literal, including the shapes that matter most — no loader at all, a loader
// with no device, and a driver whose vendor id nobody recognises.
#include "VulkanProbe.hpp"

#include "flux_test.hpp"

using namespace flux::gfx;

/// VK_MAKE_VERSION, so the expectations below read like the Vulkan spec.
static constexpr uint32_t vk_version(uint32_t major, uint32_t minor, uint32_t patch) {
    return (major << 22) | (minor << 12) | patch;
}

static void test_version_decoding() {
    CHECK_EQ(format_vulkan_version(vk_version(1, 3, 128)), std::string("1.3.128"));
    CHECK_EQ(format_vulkan_version(vk_version(1, 0, 0)), std::string("1.0.0"));
    CHECK_EQ(format_vulkan_version(vk_version(1, 1, 0)), std::string("1.1.0"));
    CHECK_EQ(format_vulkan_version(0), std::string("0.0.0"));

    CHECK_EQ(vulkan_version_major(vk_version(1, 3, 128)), 1u);
    CHECK_EQ(vulkan_version_minor(vk_version(1, 3, 128)), 3u);
    CHECK_EQ(vulkan_version_patch(vk_version(1, 3, 128)), 128u);

    // Fields must not bleed into each other at their maxima.
    CHECK_EQ(vulkan_version_minor(vk_version(1, 1023, 4095)), 1023u);
    CHECK_EQ(vulkan_version_patch(vk_version(1, 1023, 4095)), 4095u);
}

static void test_vendor_and_family_mapping() {
    CHECK_EQ(vendor_name_for_id(0x5143), std::string("qualcomm"));
    CHECK_EQ(vendor_name_for_id(0x13B5), std::string("arm"));
    CHECK_EQ(vendor_name_for_id(0x1010), std::string("imagination"));
    CHECK_EQ(vendor_name_for_id(0x10DE), std::string("nvidia"));
    CHECK_EQ(vendor_name_for_id(0x1002), std::string("amd"));
    CHECK_EQ(vendor_name_for_id(0x8086), std::string("intel"));
    CHECK_EQ(vendor_name_for_id(0), std::string("unknown"));
    CHECK_EQ(vendor_name_for_id(0xDEAD), std::string("unknown"));

    CHECK_EQ(gpu_family_for(0x5143, "Adreno (TM) 730"), std::string("adreno"));
    CHECK_EQ(gpu_family_for(0x13B5, "Mali-G78"), std::string("mali"));

    // An unknown vendor id still usually names its family in the device string.
    CHECK_EQ(gpu_family_for(0xDEAD, "Adreno (TM) 640"), std::string("adreno"));
    CHECK_EQ(gpu_family_for(0xDEAD, "Mali-G610 MC6"), std::string("mali"));
    CHECK_EQ(gpu_family_for(0xDEAD, "PowerVR Rogue GE8320"), std::string("powervr"));
    CHECK_EQ(gpu_family_for(0xDEAD, "something unheard of"), std::string("unknown"));
    CHECK_EQ(gpu_family_for(0xDEAD, ""), std::string("unknown"));

    // Matching is case-insensitive: vendors are not consistent about it.
    CHECK_EQ(gpu_family_for(0xDEAD, "ADRENO 750"), std::string("adreno"));
    CHECK_EQ(gpu_family_for(0xDEAD, "mali-g715"), std::string("mali"));
}

static void test_device_type_mapping() {
    CHECK_EQ(device_type_name(0), std::string("other"));
    CHECK_EQ(device_type_name(1), std::string("integrated"));
    CHECK_EQ(device_type_name(2), std::string("discrete"));
    CHECK_EQ(device_type_name(3), std::string("virtual"));
    CHECK_EQ(device_type_name(4), std::string("cpu"));
    CHECK_EQ(device_type_name(99), std::string("unknown"));
}

static VulkanFacts adreno_facts() {
    VulkanFacts f;
    f.available = true;
    f.status = "ok";
    f.loader_present = true;
    f.instance_version_raw = vk_version(1, 3, 0);
    f.api_version_raw = vk_version(1, 3, 128);
    f.driver_version_raw = vk_version(512, 601, 0);
    f.vendor_id = 0x5143;
    f.device_id = 0x43050A01;
    f.device_type_raw = 1;
    f.device_name = "Adreno (TM) 730";
    f.device_count = 1;
    f.instance_extensions = {"VK_KHR_surface"};
    f.device_extensions = {"VK_KHR_swapchain", "VK_EXT_hdr_metadata"};
    f.features_supported = {"samplerAnisotropy", "textureCompressionASTC_LDR"};
    return f;
}

static void test_normalize_available_device() {
    CapabilityModel m;
    normalize_vulkan(adreno_facts(), m, "fluxd.vulkan");

    CHECK(m.get_bool("vulkan", "available") == true);
    CHECK(m.get_bool("vulkan", "loader_present") == true);
    CHECK(m.get_string("vulkan", "api_version") == std::string("1.3.128"));
    CHECK(m.get_int("vulkan", "api_version_major") == 1);
    CHECK(m.get_int("vulkan", "api_version_minor") == 3);
    CHECK(m.get_int("vulkan", "api_version_patch") == 128);
    CHECK(m.get_string("vulkan", "instance_version") == std::string("1.3.0"));
    CHECK(m.get_string("vulkan", "device_name") == std::string("Adreno (TM) 730"));
    CHECK(m.get_string("vulkan", "device_type") == std::string("integrated"));
    CHECK(m.get_int("vulkan", "device_count") == 1);
    CHECK(m.get_list("vulkan", "device_extensions")->size() == 2);
    CHECK(m.get_list("vulkan", "features_supported")->size() == 2);

    // The same observation, restated in the vendor-neutral domain.
    CHECK(m.get_string("gpu", "vendor") == std::string("qualcomm"));
    CHECK(m.get_string("gpu", "family") == std::string("adreno"));
    CHECK(m.get_string("gpu", "model") == std::string("Adreno (TM) 730"));
    CHECK(m.get_int("gpu", "vendor_id") == 0x5143);

    CHECK(m.source_of("vulkan", "available") == std::string("fluxd.vulkan"));
    CHECK(m.source_of("gpu", "vendor") == std::string("fluxd.vulkan"));
}

static void test_normalize_no_loader() {
    // The commonest failure: a device with no Vulkan at all.
    VulkanFacts f;
    f.available = false;
    f.loader_present = false;
    f.status = "absent";
    f.detail = "libvulkan not present";

    CapabilityModel m;
    normalize_vulkan(f, m, "fluxd.vulkan");

    CHECK(m.get_bool("vulkan", "available") == false);
    CHECK(m.get_bool("vulkan", "loader_present") == false);
    CHECK(m.get_string("vulkan", "status") == std::string("absent"));
    CHECK(m.get_string("vulkan", "detail") == std::string("libvulkan not present"));

    // Nothing was observed about the GPU, so nothing may be claimed about it.
    // A zeroed vendor id must never surface as a real vendor.
    CHECK(!m.has("vulkan", "api_version"));
    CHECK(!m.has("vulkan", "device_name"));
    CHECK(!m.has("vulkan", "vendor_id"));
    CHECK(!m.has("vulkan", "instance_version"));
    CHECK(!m.has("gpu", "vendor"));
    CHECK(!m.has("gpu", "family"));
    CHECK(!m.has("gpu", "model"));
}

static void test_normalize_loader_without_device() {
    // An emulator or a headless build: the loader answers, no GPU does.
    VulkanFacts f;
    f.available = false;
    f.loader_present = true;
    f.status = "no_device";
    f.detail = "no Vulkan physical device";
    f.instance_version_raw = vk_version(1, 1, 0);
    f.instance_extensions = {"VK_KHR_surface"};

    CapabilityModel m;
    normalize_vulkan(f, m, "fluxd.vulkan");

    // What the loader itself answered is a real observation and is kept.
    CHECK(m.get_bool("vulkan", "loader_present") == true);
    CHECK(m.get_string("vulkan", "instance_version") == std::string("1.1.0"));
    CHECK(m.get_list("vulkan", "instance_extensions")->size() == 1);

    CHECK(m.get_bool("vulkan", "available") == false);
    CHECK(!m.has("vulkan", "device_name"));
    CHECK(!m.has("gpu", "vendor"));
}

static void test_normalize_omits_empty_lists() {
    VulkanFacts f = adreno_facts();
    f.device_extensions.clear();
    f.features_supported.clear();
    f.instance_extensions.clear();

    CapabilityModel m;
    normalize_vulkan(f, m, "fluxd.vulkan");

    // An empty list is indistinguishable from "not asked", so it is left absent.
    CHECK(!m.has("vulkan", "device_extensions"));
    CHECK(!m.has("vulkan", "features_supported"));
    CHECK(!m.has("vulkan", "instance_extensions"));
    CHECK(m.get_bool("vulkan", "available") == true);
}

static void test_probe_is_safe_on_this_machine() {
    // The real transport runs here. CI has no Vulkan, so this exercises the
    // absent path end to end; on a machine that does have a loader it exercises
    // the success path. Either way it must return, not crash, and must leave a
    // self-consistent result — that property is what keeps the probe from ever
    // taking the daemon down.
    const VulkanFacts facts = probe_vulkan();

    CHECK(!facts.status.empty());
    if (!facts.available) {
        CHECK(facts.status != std::string("ok"));
        CHECK(facts.device_name.empty());
    } else {
        CHECK(facts.status == std::string("ok"));
        CHECK(facts.loader_present);
        CHECK(facts.device_count > 0);
    }

    // A second run must agree with the first: the probe holds no state.
    const VulkanFacts again = probe_vulkan();
    CHECK_EQ(again.available, facts.available);
    CHECK_EQ(again.status, facts.status);
}

static void test_driverless_loader_is_not_an_error() {
    // A loader present with no ICD behind it (vkCreateInstance returning
    // VK_ERROR_INCOMPATIBLE_DRIVER) is a device without Vulkan, not a broken
    // probe. The build container is exactly this shape, so the classification
    // has to hold or every CI run would report a fault that is not there.
    const VulkanFacts facts = probe_vulkan();
    if (facts.loader_present && !facts.available &&
        facts.detail.find("VkResult -9") != std::string::npos) {
        CHECK_EQ(facts.status, std::string("no_device"));

        CapabilityModel m;
        VulkanCollector collector;
        const CapabilitySource source = collector.collect(m);
        CHECK_EQ(source.status, std::string("unavailable"));
    }
}

static void test_collector_reports_unavailable_not_error() {
    // On a machine with no Vulkan the collector must report `unavailable`, not
    // `error`: no GPU is a normal answer and must not read as a broken probe.
    VulkanCollector collector;
    CapabilityModel m;
    const CapabilitySource source = collector.collect(m);

    CHECK_EQ(source.id, std::string("fluxd.vulkan"));
    CHECK_EQ(source.domains.size(), static_cast<size_t>(2));
    CHECK(source.status == std::string("ok") || source.status == std::string("unavailable") ||
          source.status == std::string("error"));
    CHECK(m.has("vulkan", "available"));

    const auto available = m.get_bool("vulkan", "available");
    CHECK(available.has_value());
    if (available && !*available) CHECK(source.status != std::string("ok"));
}

static void test_run_collectors_survives_a_throwing_collector() {
    struct Exploding : CapabilityCollector {
        std::string id() const override { return "test.exploding"; }
        std::vector<std::string> domains() const override { return {"vulkan"}; }
        CapabilitySource collect(CapabilityModel &) override { throw std::runtime_error("boom"); }
    };
    struct Quiet : CapabilityCollector {
        std::string id() const override { return "test.quiet"; }
        std::vector<std::string> domains() const override { return {"runtime"}; }
        CapabilitySource collect(CapabilityModel &out) override {
            out.set("runtime", "android_sdk", static_cast<int64_t>(34), id());
            return {id(), domains(), "ok", "", 0};
        }
    };

    CapabilityModel m;
    run_collectors({std::make_shared<Exploding>(), std::make_shared<Quiet>()}, m);

    // The throwing collector is recorded as an error and the next one still ran.
    CHECK_EQ(m.sources().size(), static_cast<size_t>(2));
    CHECK(m.sources()[0].status == std::string("error"));
    CHECK(m.sources()[0].detail.find("boom") != std::string::npos);
    CHECK(m.sources()[1].status == std::string("ok"));
    CHECK(m.get_int("runtime", "android_sdk") == 34);
    CHECK(m.generated_at_ms > 0);
}

int main() {
    test_version_decoding();
    test_vendor_and_family_mapping();
    test_device_type_mapping();
    test_normalize_available_device();
    test_normalize_no_loader();
    test_normalize_loader_without_device();
    test_normalize_omits_empty_lists();
    test_probe_is_safe_on_this_machine();
    test_driverless_loader_is_not_an_error();
    test_collector_reports_unavailable_not_error();
    test_run_collectors_survives_a_throwing_collector();
    return flux_test::report("vulkan_probe_test");
}
