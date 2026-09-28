// Verifies that Flux's C++ schema binding matches the canonical descriptor.
//
// SynthesisCore owns the capability model: schema/capability_schema_v4.json in
// that repo is authoritative and jni/gfx/capability_schema_v4.json is a vendored
// copy. CapabilitySchema.hpp is a hand-written binding to it, and a hand-written
// binding drifts. This test makes drift a build failure instead of a field that
// silently stops being written.
#include "CapabilitySchema.hpp"

#include "flux_test.hpp"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#ifndef FLUX_SCHEMA_JSON
#error "FLUX_SCHEMA_JSON must point at the vendored descriptor (set by CMakeLists.txt)"
#endif

namespace schema = flux::gfx::schema;

/// The keys this build knows, by domain. Kept in the same order as the header.
static std::map<std::string, std::set<std::string>> binding_keys() {
    return {
        {schema::domain::kVulkan,
         {
             schema::vulkan::kAvailable,
             schema::vulkan::kStatus,
             schema::vulkan::kDetail,
             schema::vulkan::kLoaderPresent,
             schema::vulkan::kInstanceVersion,
             schema::vulkan::kApiVersion,
             schema::vulkan::kApiVersionMajor,
             schema::vulkan::kApiVersionMinor,
             schema::vulkan::kApiVersionPatch,
             schema::vulkan::kDriverVersionRaw,
             schema::vulkan::kDeviceCount,
             schema::vulkan::kDeviceName,
             schema::vulkan::kDeviceType,
             schema::vulkan::kVendorId,
             schema::vulkan::kDeviceId,
             schema::vulkan::kInstanceExtensions,
             schema::vulkan::kDeviceExtensions,
             schema::vulkan::kFeaturesSupported,
         }},
        {schema::domain::kGpu,
         {
             schema::gpu::kVendor,
             schema::gpu::kFamily,
             schema::gpu::kModel,
             schema::gpu::kVendorId,
             schema::gpu::kDeviceId,
             schema::gpu::kDriverVersion,
             schema::gpu::kKgslPresent,
             schema::gpu::kDevfreqPresent,
         }},
        {schema::domain::kDisplay,
         {
             schema::display::kWidthPx,
             schema::display::kHeightPx,
             schema::display::kDensityDpi,
             schema::display::kRefreshRateHz,
             schema::display::kMinRefreshRateHz,
             schema::display::kPeakRefreshRateHz,
             schema::display::kSupportedModes,
             schema::display::kHdrTypes,
             schema::display::kWideColorGamut,
         }},
        {schema::domain::kHwc,
         {
             schema::hwc::kServicePresent,
             schema::hwc::kInterface,
             schema::hwc::kComposerVersion,
             schema::hwc::kVsyncPeriodNs,
         }},
        {schema::domain::kRenderEngine,
         {
             schema::renderengine::kBackend,
             schema::renderengine::kHwuiRenderer,
             schema::renderengine::kGpuComposition,
         }},
        {schema::domain::kRuntime,
         {
             schema::runtime::kAndroidSdk,
             schema::runtime::kKernelIsGki,
             schema::runtime::kSocModel,
             schema::runtime::kSocManufacturer,
             schema::runtime::kAbi,
             schema::runtime::kPageSize,
         }},
    };
}

static rapidjson::Document load_descriptor() {
    std::ifstream f(FLUX_SCHEMA_JSON);
    rapidjson::Document doc;
    if (!f.is_open()) {
        flux_test::fail(__FILE__, __LINE__, std::string("cannot open ") + FLUX_SCHEMA_JSON);
        return doc;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    doc.Parse(ss.str().c_str());
    if (doc.HasParseError()) {
        flux_test::fail(__FILE__, __LINE__, "descriptor is not valid JSON");
    }
    return doc;
}

static void test_version_matches(const rapidjson::Document &doc) {
    CHECK(doc.HasMember("schema_version"));
    if (!doc.HasMember("schema_version")) return;
    CHECK_EQ(doc["schema_version"].GetInt(), schema::kVersion);
}

static void test_domains_and_keys_match(const rapidjson::Document &doc) {
    CHECK(doc.HasMember("domains") && doc["domains"].IsObject());
    if (!doc.HasMember("domains") || !doc["domains"].IsObject()) return;

    const auto binding = binding_keys();

    std::set<std::string> descriptor_domains;
    for (const auto &dm : doc["domains"].GetObject()) {
        const std::string domain = dm.name.GetString();
        descriptor_domains.insert(domain);

        auto bound = binding.find(domain);
        if (bound == binding.end()) {
            flux_test::fail(__FILE__, __LINE__, "descriptor domain '" + domain + "' has no binding in CapabilitySchema.hpp");
            continue;
        }

        CHECK(dm.value.HasMember("keys") && dm.value["keys"].IsObject());
        if (!dm.value.HasMember("keys") || !dm.value["keys"].IsObject()) continue;

        std::set<std::string> descriptor_keys;
        for (const auto &km : dm.value["keys"].GetObject()) {
            const std::string key = km.name.GetString();
            descriptor_keys.insert(key);
            if (!bound->second.count(key)) {
                flux_test::fail(__FILE__, __LINE__,
                                "descriptor key '" + domain + "." + key + "' is missing from CapabilitySchema.hpp");
            }
        }
        for (const auto &key : bound->second) {
            if (!descriptor_keys.count(key)) {
                flux_test::fail(__FILE__, __LINE__,
                                "binding key '" + domain + "." + key + "' is not in the canonical descriptor");
            }
        }
    }

    for (const auto &[domain, keys] : binding) {
        (void)keys;
        if (!descriptor_domains.count(domain)) {
            flux_test::fail(__FILE__, __LINE__, "binding domain '" + domain + "' is not in the canonical descriptor");
        }
    }
}

static void test_enum_values_match(const rapidjson::Document &doc) {
    CHECK(doc.HasMember("enums") && doc["enums"].IsObject());
    if (!doc.HasMember("enums") || !doc["enums"].IsObject()) return;

    const std::map<std::string, std::vector<std::string>> bound = {
        {"vulkan.status",
         {schema::vulkan::kStatusOk, schema::vulkan::kStatusAbsent, schema::vulkan::kStatusNoDevice,
          schema::vulkan::kStatusError}},
        {"hwc.interface",
         {schema::hwc::kInterfaceHidl, schema::hwc::kInterfaceAidl, schema::hwc::kInterfaceUnknown}},
    };

    for (const auto &[name, values] : bound) {
        if (!doc["enums"].HasMember(name.c_str())) {
            flux_test::fail(__FILE__, __LINE__, "descriptor has no enum '" + name + "'");
            continue;
        }
        std::set<std::string> allowed;
        for (const auto &v : doc["enums"][name.c_str()].GetArray()) allowed.insert(v.GetString());
        for (const auto &v : values) {
            if (!allowed.count(v)) {
                flux_test::fail(__FILE__, __LINE__, "'" + v + "' is not an allowed value of " + name);
            }
        }
    }
}

static void test_source_status_matches(const rapidjson::Document &doc) {
    CHECK(doc.HasMember("source_status") && doc["source_status"].IsArray());
    if (!doc.HasMember("source_status") || !doc["source_status"].IsArray()) return;

    std::set<std::string> allowed;
    for (const auto &v : doc["source_status"].GetArray()) allowed.insert(v.GetString());

    for (const char *v : {schema::source_status::kOk, schema::source_status::kPartial,
                          schema::source_status::kUnavailable, schema::source_status::kError}) {
        if (!allowed.count(v)) {
            flux_test::fail(__FILE__, __LINE__, std::string("source status '") + v + "' is not in the descriptor");
        }
    }
    CHECK_EQ(allowed.size(), static_cast<size_t>(4));
}

int main() {
    const rapidjson::Document doc = load_descriptor();
    if (doc.HasParseError() || !doc.IsObject()) return flux_test::report("capability_schema_test");

    test_version_matches(doc);
    test_domains_and_keys_match(doc);
    test_enum_values_match(doc);
    test_source_status_matches(doc);
    return flux_test::report("capability_schema_test");
}
