// Host test for jni/gfx/PlatformProbe.cpp.
//
// Each collector reaches the device only through SystemQuery, so a fake property
// table and a fake set of paths stand in for a real phone. That keeps these tests
// off the build machine's own /sys and /proc entirely.
#include "PlatformProbe.hpp"

#include "flux_test.hpp"

#include <map>
#include <set>

using namespace flux::gfx;

namespace {

/// A fake device: whatever is in the maps exists, everything else does not.
struct FakeDevice {
    std::map<std::string, std::string> properties;
    std::set<std::string> paths;
    std::map<std::string, std::string> files;

    SystemQuery query() const {
        SystemQuery q;
        q.get_property = [this](const std::string &key) {
            auto it = properties.find(key);
            return it != properties.end() ? it->second : std::string();
        };
        q.path_exists = [this](const std::string &path) { return paths.count(path) > 0; };
        q.read_file = [this](const std::string &path) {
            auto it = files.find(path);
            return it != files.end() ? it->second : std::string();
        };
        return q;
    }
};

} // namespace

static void test_gki_detection() {
    CHECK(kernel_release_is_gki("5.10.198-android12-9-00001-g1234"));
    CHECK(kernel_release_is_gki("6.1.75-android14-11-something"));
    CHECK(kernel_release_is_gki("5.15.123-android13-8-"));

    CHECK(!kernel_release_is_gki("4.14.190-perf+"));
    CHECK(!kernel_release_is_gki("5.4.233-qgki-lineage"));
    CHECK(!kernel_release_is_gki(""));

    // "-android" with no version digits, or with nothing after them, is not a
    // GKI release string.
    CHECK(!kernel_release_is_gki("5.10.0-android"));
    CHECK(!kernel_release_is_gki("5.10.0-android-x"));
    CHECK(!kernel_release_is_gki("5.10.0-android12"));
}

static void test_refresh_rate_parsing() {
    CHECK(parse_refresh_rate("120.0") == 120.0);
    CHECK(parse_refresh_rate("60") == 60.0);
    CHECK(parse_refresh_rate(" 90.5 ") == 90.5);

    // Anything that is not a positive rate reads as "not observed".
    CHECK(parse_refresh_rate("") == 0.0);
    CHECK(parse_refresh_rate("abc") == 0.0);
    CHECK(parse_refresh_rate("0") == 0.0);
    CHECK(parse_refresh_rate("-60") == 0.0);
    CHECK(parse_refresh_rate("120hz") == 0.0);
}

static void test_display_collector() {
    FakeDevice device;
    device.properties["ro.surface_flinger.min_refresh_rate"] = "60.0";
    device.properties["ro.surface_flinger.max_refresh_rate"] = "120.0";
    device.properties["ro.surface_flinger.has_wide_color_display"] = "true";
    device.properties["ro.sf.lcd_density"] = "440";

    CapabilityModel m;
    DisplayCollector collector(device.query());
    const CapabilitySource source = collector.collect(m);

    CHECK(m.get_double("display", "min_refresh_rate_hz") == 60.0);
    CHECK(m.get_double("display", "peak_refresh_rate_hz") == 120.0);
    CHECK(m.get_bool("display", "wide_color_gamut") == true);
    CHECK(m.get_int("display", "density_dpi") == 440);

    // Geometry needs SurfaceFlinger, so it stays absent and the run says so.
    CHECK(!m.has("display", "width_px"));
    CHECK(!m.has("display", "supported_modes"));
    CHECK_EQ(source.status, std::string("partial"));
}

static void test_display_collector_on_bare_device() {
    FakeDevice device;
    CapabilityModel m;
    DisplayCollector collector(device.query());
    const CapabilitySource source = collector.collect(m);

    CHECK_EQ(source.status, std::string("unavailable"));
    CHECK_EQ(m.fact_count(), static_cast<size_t>(0));
}

static void test_wide_color_false_is_recorded() {
    // "false" is an observation, distinct from the property being unset.
    FakeDevice device;
    device.properties["ro.surface_flinger.has_wide_color_display"] = "false";

    CapabilityModel m;
    DisplayCollector(device.query()).collect(m);
    CHECK(m.get_bool("display", "wide_color_gamut") == false);
}

static void test_hwc_collector_aidl() {
    FakeDevice device;
    device.paths.insert("/vendor/bin/hw/android.hardware.graphics.composer3-service");

    CapabilityModel m;
    const CapabilitySource source = HwcCollector(device.query()).collect(m);

    CHECK(m.get_bool("hwc", "service_present") == true);
    CHECK(m.get_string("hwc", "interface") == std::string("aidl"));
    CHECK(m.get_string("hwc", "composer_version") == std::string("3"));
    CHECK(!m.has("hwc", "vsync_period_ns"));
    CHECK_EQ(source.status, std::string("partial"));
}

static void test_hwc_collector_hidl() {
    FakeDevice device;
    device.paths.insert("/vendor/bin/hw/android.hardware.graphics.composer@2.4-service");

    CapabilityModel m;
    HwcCollector(device.query()).collect(m);

    CHECK(m.get_string("hwc", "interface") == std::string("hidl"));
    CHECK(m.get_string("hwc", "composer_version") == std::string("2.4"));
}

static void test_hwc_prefers_newest_interface() {
    // A ROM may ship both; the AIDL service is the one in use.
    FakeDevice device;
    device.paths.insert("/vendor/bin/hw/android.hardware.graphics.composer@2.4-service");
    device.paths.insert("/vendor/bin/hw/android.hardware.graphics.composer3-service");

    CapabilityModel m;
    HwcCollector(device.query()).collect(m);
    CHECK(m.get_string("hwc", "interface") == std::string("aidl"));
}

static void test_hwc_absent_does_not_assert_absence() {
    // Not finding the binary is weak evidence. The collector must not claim
    // there is no composer, or a framework provider could never correct it.
    FakeDevice device;
    CapabilityModel m;
    const CapabilitySource source = HwcCollector(device.query()).collect(m);

    CHECK(!m.has("hwc", "service_present"));
    CHECK(m.get_string("hwc", "interface") == std::string("unknown"));
    CHECK_EQ(source.status, std::string("unavailable"));
}

static void test_renderengine_collector_reports_without_deciding() {
    FakeDevice device;
    device.properties["debug.renderengine.backend"] = "skiaglthreaded";
    device.properties["debug.hwui.renderer"] = "skiagl";

    CapabilityModel m;
    const CapabilitySource source = RenderEngineCollector(device.query()).collect(m);

    CHECK(m.get_string("renderengine", "backend") == std::string("skiaglthreaded"));
    CHECK(m.get_string("renderengine", "hwui_renderer") == std::string("skiagl"));
    CHECK_EQ(source.status, std::string("ok"));

    // PHASE 1 collects facts only: nothing here may suggest or rank a backend.
    CHECK(!m.has("renderengine", "recommended_backend"));
    CHECK(!m.has("renderengine", "preferred_backend"));
    CHECK_EQ(m.domains().at("renderengine").values.size(), static_cast<size_t>(2));
}

static void test_renderengine_unset_means_platform_default() {
    FakeDevice device;
    CapabilityModel m;
    const CapabilitySource source = RenderEngineCollector(device.query()).collect(m);

    CHECK_EQ(source.status, std::string("unavailable"));
    CHECK(!m.has("renderengine", "backend"));
}

static void test_runtime_collector() {
    FakeDevice device;
    device.properties["ro.build.version.sdk"] = "34";
    device.properties["ro.soc.model"] = "SM8450";
    device.properties["ro.soc.manufacturer"] = "QTI";
    device.properties["ro.product.cpu.abi"] = "arm64-v8a";

    CapabilityModel m;
    const CapabilitySource source = RuntimeCollector(device.query()).collect(m);

    CHECK(m.get_int("runtime", "android_sdk") == 34);
    CHECK(m.get_string("runtime", "soc_model") == std::string("SM8450"));
    CHECK(m.get_string("runtime", "soc_manufacturer") == std::string("QTI"));
    CHECK(m.get_string("runtime", "abi") == std::string("arm64-v8a"));

    // Page size and the kernel release come from this process, so they are
    // observable even on a machine with no Android properties at all.
    CHECK(m.get_int("runtime", "page_size").value_or(0) > 0);
    CHECK(m.has("runtime", "kernel_is_gki"));
    CHECK_EQ(source.status, std::string("ok"));
}

static void test_runtime_collector_ignores_unparsable_sdk() {
    FakeDevice device;
    device.properties["ro.build.version.sdk"] = "not-a-number";

    CapabilityModel m;
    RuntimeCollector(device.query()).collect(m);
    CHECK(!m.has("runtime", "android_sdk"));
}

static void test_gpu_sysfs_collector() {
    FakeDevice device;
    device.paths.insert("/sys/class/kgsl/kgsl-3d0");
    device.paths.insert("/sys/class/kgsl/kgsl-3d0/devfreq");

    CapabilityModel m;
    GpuSysfsCollector(device.query()).collect(m);

    CHECK(m.get_bool("gpu", "kgsl_present") == true);
    CHECK(m.get_bool("gpu", "devfreq_present") == true);

    // With no better source, kgsl implies Adreno.
    CHECK(m.get_string("gpu", "vendor") == std::string("qualcomm"));
    CHECK(m.get_string("gpu", "family") == std::string("adreno"));
}

static void test_gpu_sysfs_defers_to_vulkan_identity() {
    // Vulkan already answered, so the sysfs guess must not overwrite it.
    FakeDevice device;
    device.paths.insert("/sys/class/kgsl/kgsl-3d0");

    CapabilityModel m;
    m.set("gpu", "vendor", std::string("arm"), "fluxd.vulkan");
    m.set("gpu", "family", std::string("mali"), "fluxd.vulkan");

    GpuSysfsCollector(device.query()).collect(m);

    CHECK(m.get_string("gpu", "vendor") == std::string("arm"));
    CHECK(m.get_string("gpu", "family") == std::string("mali"));
    CHECK(m.source_of("gpu", "vendor") == std::string("fluxd.vulkan"));
    CHECK(m.source_of("gpu", "kgsl_present") == std::string("fluxd.gpu_sysfs"));
}

static void test_collectors_compose_into_one_model() {
    FakeDevice device;
    device.properties["ro.build.version.sdk"] = "34";
    device.properties["debug.renderengine.backend"] = "skiagl";
    device.properties["ro.surface_flinger.min_refresh_rate"] = "60.0";
    device.paths.insert("/vendor/bin/hw/android.hardware.graphics.composer3-service");
    device.paths.insert("/sys/class/kgsl/kgsl-3d0");

    const SystemQuery q = device.query();
    CapabilityModel m;
    run_collectors(
        {
            std::make_shared<RuntimeCollector>(q),
            std::make_shared<DisplayCollector>(q),
            std::make_shared<HwcCollector>(q),
            std::make_shared<RenderEngineCollector>(q),
            std::make_shared<GpuSysfsCollector>(q),
        },
        m);

    CHECK_EQ(m.sources().size(), static_cast<size_t>(5));
    CHECK(m.get_int("runtime", "android_sdk") == 34);
    CHECK(m.get_double("display", "min_refresh_rate_hz") == 60.0);
    CHECK(m.get_string("hwc", "interface") == std::string("aidl"));
    CHECK(m.get_string("renderengine", "backend") == std::string("skiagl"));
    CHECK(m.get_bool("gpu", "kgsl_present") == true);

    // Every fact is attributed to the collector that observed it.
    CHECK(m.source_of("runtime", "android_sdk") == std::string("fluxd.runtime"));
    CHECK(m.source_of("hwc", "interface") == std::string("fluxd.hwc"));
    CHECK(m.source_of("gpu", "kgsl_present") == std::string("fluxd.gpu_sysfs"));

    // The whole thing must survive a JSON round trip unchanged.
    CapabilityModel parsed;
    std::string error;
    CHECK(CapabilityModel::from_json(m.to_json(), parsed, error));
    CHECK_EQ(parsed.fact_count(), m.fact_count());
    CHECK(parsed.to_json() == m.to_json());
}

static void test_default_system_query_is_usable_on_host() {
    // The real query must work off device: properties read as absent rather than
    // crashing, and the filesystem probes answer normally.
    const SystemQuery q = default_system_query();
    CHECK(q.get_property("ro.build.version.sdk").empty());
    CHECK(q.path_exists("/"));
    CHECK(!q.path_exists("/definitely/not/a/real/path"));
    CHECK(q.read_file("/definitely/not/a/real/path").empty());
}

int main() {
    test_gki_detection();
    test_refresh_rate_parsing();
    test_display_collector();
    test_display_collector_on_bare_device();
    test_wide_color_false_is_recorded();
    test_hwc_collector_aidl();
    test_hwc_collector_hidl();
    test_hwc_prefers_newest_interface();
    test_hwc_absent_does_not_assert_absence();
    test_renderengine_collector_reports_without_deciding();
    test_renderengine_unset_means_platform_default();
    test_runtime_collector();
    test_runtime_collector_ignores_unparsable_sdk();
    test_gpu_sysfs_collector();
    test_gpu_sysfs_defers_to_vulkan_identity();
    test_collectors_compose_into_one_model();
    test_default_system_query_is_usable_on_host();
    return flux_test::report("platform_probe_test");
}
