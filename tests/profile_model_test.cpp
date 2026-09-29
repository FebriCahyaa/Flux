// Host tests for performance profile inheritance (jni/perf/ProfileModel.*).

#include "flux_test.hpp"

#include "ProfileModel.hpp"

using namespace flux::perf;

namespace {

bool contains(const std::vector<std::string> &v, const std::string &needle) {
    for (const auto &s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

const char *kDoc = R"({
  "version": 1,
  "global":  { "memory": "balanced", "refresh": "real", "profile": "performance" },
  "presets": {
    "gaming":      { "memory": "gaming", "storage": "gaming", "launch_boost": true },
    "competitive": { "extends": "gaming", "touch": "competitive", "refresh": "hz120" }
  },
  "games": {
    "com.a": { "extends": "competitive", "touch": "responsive" },
    "com.b": { "storage": "balanced" }
  }
})";

void test_inheritance_order() {
    ProfileDocument doc;
    auto lr = load_document(kDoc, doc);
    CHECK(lr.ok);
    CHECK(lr.format == Format::Current);

    auto r = resolve(doc, "com.a");
    CHECK(r.ok());
    CHECK_EQ(r.perf.memory, std::string("gaming"));       // preset gaming over global
    CHECK_EQ(r.perf.storage, std::string("gaming"));      // preset gaming
    CHECK_EQ(r.perf.touch, std::string("responsive"));    // game over preset competitive
    CHECK_EQ(r.perf.refresh, std::string("hz120"));       // preset competitive over global
    CHECK(r.perf.launch_boost);                           // inherited from grandparent preset
    CHECK_EQ(r.profile, std::string("performance"));      // global
    CHECK_EQ(r.sources["memory"].describe(), std::string("preset gaming"));
    CHECK_EQ(r.sources["touch"].describe(), std::string("game override"));
    CHECK_EQ(r.sources["refresh"].describe(), std::string("preset competitive"));
    CHECK_EQ(r.sources["profile"].describe(), std::string("global"));
    CHECK_EQ(r.chain.size(), size_t{5}); // builtin, global, gaming, competitive, game

    // Game without preset: partial override, everything else inherits.
    auto b = resolve(doc, "com.b");
    CHECK_EQ(b.perf.storage, std::string("balanced"));
    CHECK_EQ(b.perf.memory, std::string("balanced"));
    CHECK_EQ(b.perf.touch, std::string("default"));
    CHECK_EQ(b.sources["touch"].describe(), std::string("builtin default"));

    // Unknown package: global + builtin only.
    auto u = resolve(doc, "com.unknown");
    CHECK(u.ok());
    CHECK_EQ(u.perf.memory, std::string("balanced"));
    CHECK_EQ(u.sources["memory"].describe(), std::string("global"));

    // Explanation output.
    const std::string e = r.explain();
    CHECK(e.find("memory:\n  source=preset gaming") != std::string::npos);
    CHECK(e.find("touch:\n  source=game override") != std::string::npos);
    CHECK(e.find("profile:\n  source=global") != std::string::npos);
}

void test_override_priority() {
    ProfileDocument doc;
    CHECK(load_document(kDoc, doc).ok);
    ProfileLayer runtime;
    runtime.memory = "gaming_plus";
    auto r = resolve(doc, "com.a", runtime);
    CHECK_EQ(r.perf.memory, std::string("gaming_plus"));
    CHECK_EQ(r.sources["memory"].describe(), std::string("runtime override"));
    CHECK_EQ(r.perf.touch, std::string("responsive")); // untouched by runtime
}

void test_missing_parent_and_unknown_profile() {
    ProfileDocument doc;
    CHECK(load_document(R"({"version":1,
        "presets": { "child": { "extends": "ghost", "memory": "gaming" } },
        "games": { "com.a": { "extends": "child", "touch": "balanced" },
                   "com.b": { "extends": "nope" } } })",
                        doc)
              .ok);
    auto a = resolve(doc, "com.a");
    CHECK(!a.ok());
    CHECK(contains(a.errors, "missing parent 'ghost'"));
    CHECK_EQ(a.perf.memory, std::string("gaming"));  // chain applied up to the break
    CHECK_EQ(a.perf.touch, std::string("balanced")); // game layer still applies

    auto b = resolve(doc, "com.b");
    CHECK(!b.ok());
    CHECK(contains(b.errors, "unknown profile 'nope'"));
}

void test_cycle_detection() {
    ProfileDocument doc;
    CHECK(load_document(R"({"version":1,
        "presets": { "x": { "extends": "y", "memory": "gaming" },
                     "y": { "extends": "x", "touch": "balanced" },
                     "self": { "extends": "self" } },
        "games": { "com.a": { "extends": "x" }, "com.s": { "extends": "self" } } })",
                        doc)
              .ok);
    auto a = resolve(doc, "com.a");
    CHECK(!a.ok());
    CHECK(contains(a.errors, "inheritance cycle"));
    auto s = resolve(doc, "com.s");
    CHECK(contains(s.errors, "inheritance cycle"));
}

void test_invalid_profile() {
    ProfileDocument doc;
    auto lr = load_document(R"({"version":1,
        "global": { "memory": "ultra" },
        "presets": { "bad": { "turbo": true }, "ok": { "memory": "gaming" },
                     "typed": { "launch_boost": "yes" } },
        "games": { "com.a": { "extends": "ok", "refresh": "hz999" },
                   "com.b": { "refresh": "custom", "refresh_custom_hz": 0 } } })",
                            doc);
    CHECK(!lr.ok);
    CHECK(contains(lr.errors, "invalid value 'ultra' for memory"));
    CHECK(contains(lr.errors, "invalid field 'turbo'"));
    CHECK(contains(lr.errors, "launch_boost must be a boolean"));
    CHECK(contains(lr.errors, "invalid value 'hz999' for refresh"));
    CHECK(contains(lr.errors, "refresh_custom_hz"));
    CHECK_EQ(doc.presets.count("bad"), size_t{0});  // rejected entries are not loaded
    CHECK_EQ(doc.presets.count("ok"), size_t{1});   // valid entries still load
    CHECK(!doc.global.memory.has_value());

    ProfileDocument d2;
    CHECK(!load_document("not json", d2).ok);
    CHECK(!load_document(R"({"version": 7})", d2).ok);
    CHECK(!load_document(R"([1,2])", d2).ok);
}

void test_legacy_profile_compatibility() {
    // Old Game Runtime branch game_profiles.json: nested "performance", plus "compatibility".
    ProfileDocument doc;
    auto lr = load_document(R"({
      "com.a": { "package": "com.a", "extends": "competitive",
                 "performance": { "profile": "performance", "touch": "responsive_plus", "launch_boost": true },
                 "compatibility": { "mode": "auto", "gpu_profile": "some_gpu" } },
      "com.b": { "performance": { "touch": "custom" } } })",
                            doc);
    CHECK(lr.ok);
    CHECK(lr.format == Format::LegacyGameProfiles);
    CHECK(contains(lr.warnings, "compatibility ignored"));
    CHECK(contains(lr.warnings, "touch 'custom' not supported"));
    CHECK_EQ(doc.games["com.a"].touch.value_or(""), std::string("responsive_plus"));
    CHECK_EQ(doc.games["com.a"].extends, std::string("competitive"));
    CHECK(!doc.games["com.b"].touch.has_value());

    // Old compat_library.json: presets usable, identities ignored.
    ProfileDocument lib;
    auto ll = load_document(R"({
      "presets": { "competitive": { "performance": { "memory": "gaming_plus", "refresh": "hz120" } } },
      "identities": { "flagship": { "layer": "gpu", "fields": { "renderer": "X" } } } })",
                            lib);
    CHECK(ll.ok);
    CHECK(ll.format == Format::LegacyLibrary);
    CHECK(contains(ll.warnings, "identities ignored"));
    merge_document(doc, lib);
    auto r = resolve(doc, "com.a");
    CHECK(r.ok());
    CHECK_EQ(r.perf.memory, std::string("gaming_plus"));
    CHECK_EQ(r.perf.touch, std::string("responsive_plus"));
    CHECK_EQ(r.sources["memory"].describe(), std::string("preset competitive"));

    // main's gamelist.json lite_mode.
    ProfileDocument gl;
    gl.games["com.lite"] = layer_from_gamelist(true);
    gl.games["com.full"] = layer_from_gamelist(false);
    CHECK_EQ(resolve(gl, "com.lite").profile, std::string("performance_lite"));
    CHECK_EQ(resolve(gl, "com.full").profile, std::string("performance"));
    CHECK_EQ(resolve(gl, "com.full").sources["profile"].describe(), std::string("builtin default"));
}

} // namespace

int main() {
    test_inheritance_order();
    test_override_priority();
    test_missing_parent_and_unknown_profile();
    test_cycle_detection();
    test_invalid_profile();
    test_legacy_profile_compatibility();
    return flux_test::report("profile_model_test");
}
