// Host test for jni/gfx/CapabilityModel.cpp: typed storage, provenance, merge
// precedence, JSON round trips and the forward-compatibility rules.
#include "CapabilityModel.hpp"

#include "flux_test.hpp"

#include <cstdio>

using namespace flux::gfx;

static void test_typed_storage() {
    CapabilityModel m;
    m.set("vulkan", "available", true, "probe");
    m.set("vulkan", "device_count", static_cast<int64_t>(2), "probe");
    m.set("display", "refresh_rate_hz", 120.0, "probe");
    m.set("gpu", "model", std::string("Adreno (TM) 730"), "probe");
    m.set("vulkan", "device_extensions", std::vector<std::string>{"VK_KHR_swapchain"}, "probe");

    CHECK(m.get_bool("vulkan", "available") == true);
    CHECK(m.get_int("vulkan", "device_count") == 2);
    CHECK(m.get_double("display", "refresh_rate_hz") == 120.0);
    CHECK(m.get_string("gpu", "model") == std::string("Adreno (TM) 730"));
    CHECK(m.get_list("vulkan", "device_extensions")->size() == 1);
    CHECK_EQ(m.fact_count(), static_cast<size_t>(5));

    // A typed accessor must not coerce across types: asking for the wrong type
    // yields nullopt rather than a plausible-looking zero.
    CHECK(!m.get_int("vulkan", "available").has_value());
    CHECK(!m.get_string("vulkan", "device_count").has_value());
    CHECK(!m.get_bool("gpu", "model").has_value());
    CHECK(!m.get_list("gpu", "model").has_value());
}

static void test_absent_is_absent() {
    CapabilityModel m;
    CHECK(!m.has("vulkan", "available"));
    CHECK(!m.get_bool("vulkan", "available").has_value());
    CHECK(m.find("vulkan", "available") == nullptr);
    CHECK(!m.source_of("vulkan", "available").has_value());
    CHECK_EQ(m.fact_count(), static_cast<size_t>(0));
}

static void test_int_readable_as_double() {
    // JSON has one number type, so a whole-numbered double comes back as an int.
    // A consumer asking for a rate must still get it.
    CapabilityModel m;
    m.set("display", "refresh_rate_hz", static_cast<int64_t>(60), "probe");
    CHECK(m.get_double("display", "refresh_rate_hz") == 60.0);
}

static void test_provenance() {
    CapabilityModel m;
    m.set("gpu", "vendor", std::string("qualcomm"), "fluxd.vulkan");
    m.set("gpu", "model", std::string("Adreno 730"), "fluxd.vulkan");
    CHECK(m.source_of("gpu", "vendor") == std::string("fluxd.vulkan"));

    // A second contributor to the same domain is recorded per key, so the domain
    // default stays with whoever established it.
    m.set("gpu", "kgsl_present", true, "fluxd.gpu_sysfs");
    CHECK(m.source_of("gpu", "kgsl_present") == std::string("fluxd.gpu_sysfs"));
    CHECK(m.source_of("gpu", "vendor") == std::string("fluxd.vulkan"));
    CHECK(m.domains().at("gpu").source == std::string("fluxd.vulkan"));

    // Rewriting a key back to the domain's own source drops the override again.
    m.set("gpu", "kgsl_present", false, "fluxd.vulkan");
    CHECK(m.source_of("gpu", "kgsl_present") == std::string("fluxd.vulkan"));
    CHECK(m.domains().at("gpu").value_source.empty());
}

static void test_merge_keeps_first_observation() {
    CapabilityModel primary;
    primary.set("gpu", "vendor", std::string("qualcomm"), "fluxd.vulkan");

    CapabilityModel secondary;
    secondary.set("gpu", "vendor", std::string("unknown"), "fluxd.gpu_sysfs");
    secondary.set("gpu", "kgsl_present", true, "fluxd.gpu_sysfs");
    secondary.add_source({"fluxd.gpu_sysfs", {"gpu"}, "ok", "", 0});

    const size_t taken = primary.merge(secondary);

    // The weaker sysfs guess must not overwrite the Vulkan observation.
    CHECK_EQ(taken, static_cast<size_t>(1));
    CHECK(primary.get_string("gpu", "vendor") == std::string("qualcomm"));
    CHECK(primary.get_bool("gpu", "kgsl_present") == true);
    CHECK(primary.source_of("gpu", "kgsl_present") == std::string("fluxd.gpu_sysfs"));
    CHECK_EQ(primary.sources().size(), static_cast<size_t>(1));
}

static void test_add_source_replaces_by_id() {
    CapabilityModel m;
    m.add_source({"fluxd.vulkan", {"vulkan"}, "unavailable", "no loader", 1});
    m.add_source({"fluxd.vulkan", {"vulkan", "gpu"}, "ok", "", 2});
    CHECK_EQ(m.sources().size(), static_cast<size_t>(1));
    CHECK(m.sources()[0].status == std::string("ok"));
    CHECK_EQ(m.sources()[0].domains.size(), static_cast<size_t>(2));
    CHECK_EQ(m.sources()[0].collected_at_ms, static_cast<int64_t>(2));
}

static void test_json_round_trip() {
    CapabilityModel m;
    m.generated_at_ms = 1730000000000;
    m.set("vulkan", "available", true, "fluxd.vulkan");
    m.set("vulkan", "api_version", std::string("1.3.128"), "fluxd.vulkan");
    m.set("vulkan", "device_count", static_cast<int64_t>(1), "fluxd.vulkan");
    m.set("vulkan", "device_extensions",
          std::vector<std::string>{"VK_KHR_swapchain", "VK_EXT_hdr_metadata"}, "fluxd.vulkan");
    m.set("display", "refresh_rate_hz", 120.5, "synthesiscore.display");
    m.set("gpu", "kgsl_present", true, "fluxd.gpu_sysfs");
    m.set("gpu", "vendor", std::string("qualcomm"), "fluxd.vulkan");
    m.add_source({"fluxd.vulkan", {"vulkan", "gpu"}, "ok", "", 1730000000001});
    m.add_source({"synthesiscore.display", {"display"}, "partial", "modes pending", 1730000000002});

    CapabilityModel parsed;
    std::string error;
    CHECK(CapabilityModel::from_json(m.to_json(), parsed, error));
    CHECK(error.empty());

    CHECK_EQ(parsed.schema_version, m.schema_version);
    CHECK_EQ(parsed.generated_at_ms, m.generated_at_ms);
    CHECK_EQ(parsed.fact_count(), m.fact_count());
    CHECK(parsed.get_bool("vulkan", "available") == true);
    CHECK(parsed.get_string("vulkan", "api_version") == std::string("1.3.128"));
    CHECK(parsed.get_int("vulkan", "device_count") == 1);
    CHECK(parsed.get_double("display", "refresh_rate_hz") == 120.5);
    CHECK(parsed.get_list("vulkan", "device_extensions")->size() == 2);

    // Provenance survives, including the per-key override inside a shared domain.
    CHECK(parsed.source_of("gpu", "vendor") == std::string("fluxd.vulkan"));
    CHECK(parsed.source_of("gpu", "kgsl_present") == std::string("fluxd.gpu_sysfs"));
    CHECK(parsed.source_of("display", "refresh_rate_hz") == std::string("synthesiscore.display"));
    CHECK_EQ(parsed.sources().size(), static_cast<size_t>(2));

    // Serialisation is deterministic, so the model is diffable across runs.
    CHECK(parsed.to_json() == m.to_json());
}

static void test_empty_model_round_trip() {
    CapabilityModel empty;
    CapabilityModel parsed;
    std::string error;
    CHECK(CapabilityModel::from_json(empty.to_json(), parsed, error));
    CHECK_EQ(parsed.fact_count(), static_cast<size_t>(0));
    CHECK_EQ(parsed.schema_version, flux::gfx::schema::kVersion);
}

static void test_rejects_malformed_json() {
    CapabilityModel out;
    std::string error;

    CHECK(!CapabilityModel::from_json("{ not json", out, error));
    CHECK(!error.empty());

    CHECK(!CapabilityModel::from_json("[1,2,3]", out, error));
    CHECK(!CapabilityModel::from_json("{}", out, error)); // no schema_version
    CHECK(!CapabilityModel::from_json("", out, error));
}

static void test_forward_compatible_with_newer_schema() {
    // A v5 producer may add domains and keys this build has never heard of. The
    // parser keeps them verbatim, so a v4 reader passing the model along cannot
    // silently destroy facts it did not understand.
    const char *future = R"({
      "schema_version": 5,
      "generated_at_ms": 7,
      "capabilities": {
        "vulkan": { "available": true, "ray_tracing_tier": 2 },
        "graphite": { "enabled": true }
      },
      "provenance": {
        "vulkan": { "_source": "fluxd.vulkan" },
        "graphite": { "_source": "synthesiscore.graphite" }
      },
      "sources": [ { "id": "fluxd.vulkan", "domains": ["vulkan"], "status": "ok",
                     "detail": "", "collected_at_ms": 7 } ]
    })";

    CapabilityModel m;
    std::string error;
    CHECK(CapabilityModel::from_json(future, m, error));
    CHECK_EQ(m.schema_version, 5);
    CHECK(m.get_bool("vulkan", "available") == true);
    CHECK(m.get_int("vulkan", "ray_tracing_tier") == 2);
    CHECK(m.get_bool("graphite", "enabled") == true);
    CHECK(m.source_of("graphite", "enabled") == std::string("synthesiscore.graphite"));
}

static void test_unrepresentable_values_are_dropped() {
    // null and nested objects have no place in the model. Dropping the key is
    // right: it reads as "not observed", which beats inventing a value.
    const char *odd = R"({
      "schema_version": 4,
      "capabilities": { "vulkan": {
        "available": true, "detail": null, "nested": {"a": 1}, "mixed": [1, "two"]
      } },
      "provenance": { "vulkan": { "_source": "x" } },
      "sources": []
    })";

    CapabilityModel m;
    std::string error;
    CHECK(CapabilityModel::from_json(odd, m, error));
    CHECK(m.get_bool("vulkan", "available") == true);
    CHECK(!m.has("vulkan", "detail"));
    CHECK(!m.has("vulkan", "nested"));
    CHECK(!m.has("vulkan", "mixed"));
}

static void test_file_round_trip() {
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                             "/flux_capability_model_test.json";
    std::remove(path.c_str());

    CapabilityModel m;
    m.set("runtime", "android_sdk", static_cast<int64_t>(34), "fluxd.runtime");
    CHECK(m.write_file(path));

    CapabilityModel loaded;
    std::string error;
    CHECK(CapabilityModel::read_file(path, loaded, error));
    CHECK(loaded.get_int("runtime", "android_sdk") == 34);

    // The temp file used for the atomic rename must not be left behind.
    CHECK(std::fopen((path + ".tmp").c_str(), "r") == nullptr);
    std::remove(path.c_str());

    CHECK(!CapabilityModel::read_file(path + ".missing", loaded, error));
    CHECK(!error.empty());
}

int main() {
    test_typed_storage();
    test_absent_is_absent();
    test_int_readable_as_double();
    test_provenance();
    test_merge_keeps_first_observation();
    test_add_source_replaces_by_id();
    test_json_round_trip();
    test_empty_model_round_trip();
    test_rejects_malformed_json();
    test_forward_compatible_with_newer_schema();
    test_unrepresentable_values_are_dropped();
    test_file_round_trip();
    return flux_test::report("capability_model_test");
}
