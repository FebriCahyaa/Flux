// Host tests for the Zygisk provider's decision logic (jni/compat/ProviderPlan.*):
// plan contract, validation, process matching, layer selection, wire formats.
// No JNI, no device: the same code runs in the provider and its root companion.

#include <set>

#include "flux_test.hpp"

#include "ProviderPlan.hpp"

using namespace flux::compat;
using namespace flux::compat::provider;

namespace {

constexpr int64_t kNow = 1'700'000'000'000;
const char *kBoot = "b00tid-1234";

ProfileLibrary library() {
    ProfileLibrary lib;
    std::string err;
    bool ok = lib.load_json(R"({"identities":{
      "dev":{"layer":"device","fields":{"MODEL":"FlagX","BRAND":"Acme","MANUFACTURER":"Acme Corp"}},
      "cpu":{"layer":"cpu","fields":{"SOC_MODEL":"acme8","SOC_MANUFACTURER":"Acme"}},
      "gpu":{"layer":"gpu","fields":{"gl_renderer":"Acme GPU 9","gl_vendor":"Acme","vk_device_name":"Acme GPU 9",
                                     "vk_vendor_id":"0x1234","vk_device_id":"77","vk_api_version":"1.1"}}
    }})", err);
    CHECK(ok);
    return lib;
}

Resolution resolution(std::set<Layer> required, Mode mode = Mode::Advanced) {
    Resolution r;
    r.package = "com.example.game";
    r.mode = mode;
    r.layers.resize(4);
    const char *names[] = {"dev", "cpu", "gpu", ""};
    for (Layer l : {Layer::Device, Layer::Cpu, Layer::Gpu, Layer::Display}) {
        auto &d = r.layers[static_cast<size_t>(l)];
        d.layer = l;
        d.required = required.count(l) > 0;
        if (d.required) d.identity = names[static_cast<size_t>(l)];
    }
    r.should_apply = !required.empty();
    return r;
}

Plan plan_for(std::set<Layer> required, Mode mode = Mode::Advanced, ProcessScope scope = {}) {
    return make_plan(resolution(required, mode), library(), kBoot, 4242, kNow, 3'600'000, scope, "game");
}

Env good_env() { return Env{kNow + 1000, kBoot, true}; }

// -- layer selection: the provider applies exactly what the resolver required -------------------

void test_layer_selection_is_exactly_the_plan() {
    auto d = plan_for({Layer::Device});
    CHECK((d.layers == std::vector<Layer>{Layer::Device}));
    CHECK(d.identities.count("device") == 1 && d.identities.count("cpu") == 0 && d.identities.count("gpu") == 0);
    auto it = items_from_plan(d, validate_plan(d, good_env()));
    CHECK(!it.build.empty() && it.gl.empty() && !it.vk.any());
    CHECK_EQ(it.build.at("MODEL"), "FlagX");
    CHECK_EQ(it.props.at("ro.product.model"), "FlagX");
    CHECK(it.build.count("SOC_MODEL") == 0); // CPU layer off

    auto dg = plan_for({Layer::Device, Layer::Gpu});
    auto ig = items_from_plan(dg, validate_plan(dg, good_env()));
    CHECK(!ig.build.empty() && !ig.gl.empty() && ig.vk.any());
    CHECK(ig.build.count("SOC_MODEL") == 0);     // CPU stays real
    CHECK_EQ(ig.gl.at("renderer"), "Acme GPU 9");

    auto c = plan_for({Layer::Cpu});
    auto ic = items_from_plan(c, validate_plan(c, good_env()));
    CHECK_EQ(ic.build.at("SOC_MODEL"), "acme8");
    CHECK(ic.build.count("MODEL") == 0);
    CHECK_EQ(ic.props.at("ro.soc.model"), "acme8");
}

void test_display_layer_never_reaches_the_provider() {
    auto p = plan_for({Layer::Display});
    CHECK(p.layers.empty());
    CHECK(!p.active);
}

void test_all_three_layers() {
    auto p = plan_for({Layer::Device, Layer::Cpu, Layer::Gpu});
    auto v = validate_plan(p, good_env());
    CHECK(v.ok());
    CHECK(v.layers.size() == 3);
    for (const auto &l : v.layers) CHECK(l.usable);
}

// -- no evidence, no plan -----------------------------------------------------------------------------

void test_unknown_game_and_mode_real_produce_inert_plans() {
    auto none = plan_for({}, Mode::Auto); // unknown game: resolver required nothing
    CHECK(!none.active);
    CHECK(none.transaction_id.empty());
    auto v = validate_plan(none, good_env());
    CHECK(!v.ok());
    CHECK(v.reject == Reject::Inactive);

    auto real = plan_for({}, Mode::Real);
    CHECK(!real.active);
    CHECK(validate_plan(real, good_env()).reject == Reject::Inactive);
}

// -- transaction / expiry / boot / daemon -------------------------------------------------------------------

void test_transaction_validation() {
    auto p = plan_for({Layer::Device});
    CHECK(!p.transaction_id.empty());
    CHECK(validate_plan(p, good_env()).ok());

    auto tampered = p;
    tampered.identities["device"]["MODEL"] = "Other"; // edited without re-arming
    CHECK(validate_plan(tampered, good_env()).reject == Reject::TransactionMismatch);

    auto missing = p;
    missing.transaction_id.clear();
    CHECK(validate_plan(missing, good_env()).reject == Reject::NoTransaction);

    // Same inputs => same id; any change => new id (a process holding the old id is recognisably stale).
    CHECK_EQ(plan_for({Layer::Device}).transaction_id, p.transaction_id);
    CHECK(plan_for({Layer::Device, Layer::Gpu}).transaction_id != p.transaction_id);
}

void test_expired_plan() {
    auto p = plan_for({Layer::Device});
    Env e = good_env();
    e.now_ms = p.expires_at_ms + 1;
    CHECK(validate_plan(p, e).reject == Reject::Expired);
    p.expires_at_ms = 0; // no time limit: boot + daemon still bind it
    p.transaction_id = compute_transaction_id(p);
    CHECK(validate_plan(p, e).ok());
}

void test_stale_plan_from_another_boot_or_dead_daemon() {
    auto p = plan_for({Layer::Device});
    Env other_boot = good_env();
    other_boot.boot_id = "another-boot";
    CHECK(validate_plan(p, other_boot).reject == Reject::BootMismatch);
    Env dead = good_env();
    dead.daemon_alive = false;
    CHECK(validate_plan(p, dead).reject == Reject::DaemonGone);
}

void test_invalid_plans() {
    Plan p;
    CHECK(validate_plan(p, good_env()).reject == Reject::WrongVersion);
    auto q = plan_for({Layer::Device});
    q.package = "../evil";
    CHECK(validate_plan(q, good_env()).reject == Reject::PackageMismatch);
    std::string err;
    Plan out;
    CHECK(!plan_from_json("not json", out, err));
    CHECK(!plan_from_json("[]", out, err));
}

void test_plan_json_round_trip_and_legacy_fields() {
    auto p = plan_for({Layer::Device, Layer::Gpu});
    std::string js = plan_to_json(p);
    // Fields the v1 spool already had are still there for older readers.
    CHECK(js.find("\"active\":true") != std::string::npos);
    CHECK(js.find("\"package\":\"com.example.game\"") != std::string::npos);
    CHECK(js.find("\"identities\"") != std::string::npos);
    Plan back;
    std::string err;
    CHECK(plan_from_json(js, back, err));
    CHECK_EQ(back.transaction_id, p.transaction_id);
    CHECK(back.layers == p.layers);
    CHECK(validate_plan(back, good_env()).ok());
}

// -- unsupported requests are refused, not faked -----------------------------------------------------------------

void test_unsupported_isa_feature_rejected() {
    auto p = plan_for({Layer::Cpu});
    p.identities["cpu"]["hwcap_features"] = "sve2"; // an ISA claim, not an identity
    p.transaction_id = compute_transaction_id(p);
    auto v = validate_plan(p, good_env());
    CHECK(v.reject == Reject::CapabilityClaim);
    CHECK(items_from_plan(p, v).empty());
}

void test_unsupported_vulkan_capability_rejected() {
    for (const char *key : {"vk_extensions", "vk_features", "vk_limits", "vk_queue_families"}) {
        auto p = plan_for({Layer::Gpu});
        p.identities["gpu"][key] = "VK_KHR_ray_tracing_pipeline";
        p.transaction_id = compute_transaction_id(p);
        auto v = validate_plan(p, good_env());
        CHECK(!v.ok());
        CHECK(items_from_plan(p, v).empty());
    }
}

void test_unknown_field_and_bad_values() {
    auto p = plan_for({Layer::Device});
    p.identities["device"]["ANDROID_ID"] = "1234"; // not a Build field the provider carries
    p.transaction_id = compute_transaction_id(p);
    auto v = validate_plan(p, good_env());
    CHECK(!v.ok());
    CHECK(v.layers[0].reason.find("unsupported field") != std::string::npos);

    auto q = plan_for({Layer::Device});
    q.identities["device"]["MODEL"] = std::string("bad\nvalue");
    q.transaction_id = compute_transaction_id(q);
    CHECK(!validate_plan(q, good_env()).ok());

    auto g = plan_for({Layer::Gpu});
    g.identities["gpu"]["vk_vendor_id"] = "not-a-number";
    g.transaction_id = compute_transaction_id(g);
    CHECK(!validate_plan(g, good_env()).ok());
    g = plan_for({Layer::Gpu});
    g.identities["gpu"]["vk_api_version"] = "banana";
    g.transaction_id = compute_transaction_id(g);
    CHECK(!validate_plan(g, good_env()).ok());
}

void test_one_bad_layer_does_not_apply_but_others_do() {
    auto p = plan_for({Layer::Device, Layer::Cpu});
    p.identities["cpu"]["neon"] = "yes";
    p.transaction_id = compute_transaction_id(p);
    auto v = validate_plan(p, good_env());
    CHECK(v.ok());
    CHECK(v.layers[0].usable && !v.layers[1].usable);
    auto it = items_from_plan(p, v);
    CHECK(it.build.count("MODEL") == 1);
    CHECK(it.build.count("SOC_MODEL") == 0);
}

void test_vulkan_api_version_never_above_real() {
    auto v13 = parse_vk_api_version("1.3.0");
    auto v11 = parse_vk_api_version("1.1");
    CHECK(v13.has_value() && v11.has_value());
    CHECK(*v11 == ((1u << 22) | (1u << 12)));
    CHECK(vk_api_version_allowed(*v11, *v13));   // lowering is fine
    CHECK(!vk_api_version_allowed(*v13, *v11));  // claiming more than the GPU has is not
    CHECK(!parse_vk_api_version("1"));
    CHECK(!parse_vk_api_version("1.x"));
    CHECK(!parse_vk_api_version("9.0"));
}

// -- process matching -----------------------------------------------------------------------------------------------

void test_package_and_uid_matching() {
    auto p = plan_for({Layer::Device});
    CHECK(match_process(p, {"com.example.game", "com.example.game", 10123}).matched);

    // A process that merely looks like the package but belongs to another UID owner is refused.
    auto m = match_process(p, {"com.example.game", "com.other.app", 10456});
    CHECK(!m.matched && m.reject == Reject::PackageMismatch);
    CHECK(!match_process(p, {"com.example.game", "", 10999}).matched); // owner unknown: never guess
    CHECK(!match_process(p, {"com.example.gameplus", "com.example.gameplus", 10500}).matched);
    CHECK(!match_process(p, {"com.example.game.evil", "com.example.game.evil", 10501}).matched);
}

void test_multi_process_scope() {
    ProcessScope main_only;
    auto p = plan_for({Layer::Device}, Mode::Advanced, main_only);
    const std::string pkg = "com.example.game";
    CHECK(match_process(p, {pkg, pkg, 1}).matched);
    CHECK(!match_process(p, {pkg + ":remote", pkg, 1}).matched);
    CHECK(match_process(p, {pkg + ":remote", pkg, 1}).reject == Reject::ProcessNotInScope);
    CHECK(!match_process(p, {pkg + ":engine", pkg, 1}).matched);
    CHECK(!match_process(p, {pkg + ":service", pkg, 1}).matched);

    ProcessScope listed{ProcessScope::Kind::Listed, {pkg + ":engine"}};
    auto l = plan_for({Layer::Device}, Mode::Advanced, listed);
    CHECK(!match_process(l, {pkg, pkg, 1}).matched);            // main not listed => untouched
    CHECK(!match_process(l, {pkg + ":remote", pkg, 1}).matched);
    CHECK(match_process(l, {pkg + ":engine", pkg, 1}).matched);
    CHECK(!match_process(l, {pkg + ":engine2", pkg, 1}).matched); // exact names, not prefixes

    ProcessScope all{ProcessScope::Kind::All, {}};
    auto a = plan_for({Layer::Device}, Mode::Advanced, all);
    CHECK(match_process(a, {pkg, pkg, 1}).matched);
    CHECK(match_process(a, {pkg + ":remote", pkg, 1}).matched);
    CHECK(!match_process(a, {pkg + "x", pkg, 1}).matched);       // "all" means the package's own processes
    CHECK(!match_process(a, {"other:remote", pkg, 1}).matched);

    // An empty 'listed' scope would match nothing by accident; it is invalid instead.
    ProcessScope empty_listed{ProcessScope::Kind::Listed, {}};
    CHECK(validate_plan(plan_for({Layer::Device}, Mode::Advanced, empty_listed), good_env()).reject == Reject::ProcessNotInScope);
}

void test_profile_selection_by_package() {
    // Two games, two plans: each process only ever sees its own.
    auto a = plan_for({Layer::Device});
    Resolution rb = resolution({Layer::Gpu});
    rb.package = "com.example.other";
    auto b = make_plan(rb, library(), kBoot, 4242, kNow, 0, {}, "other");
    CHECK(match_process(a, {"com.example.game", "com.example.game", 1}).matched);
    CHECK(!match_process(a, {"com.example.other", "com.example.other", 2}).matched);
    CHECK(match_process(b, {"com.example.other", "com.example.other", 2}).matched);
    CHECK(!match_process(b, {"com.example.game", "com.example.game", 1}).matched);
    CHECK(items_from_plan(a, validate_plan(a, good_env())).gl.empty());
    CHECK(items_from_plan(b, validate_plan(b, good_env())).build.empty());
}

// -- armed list: the cheap pre-filter --------------------------------------------------------------------------------

void test_armed_list_prefilter() {
    std::vector<ArmedEntry> e = {{"com.example.game", 10123, "tx1"}, {"com.example.other", 10200, "tx2"}};
    auto text = armed_list_to_text(e);
    auto back = armed_list_from_text(text);
    CHECK(back.size() == 2);
    CHECK_EQ(back[0].package, "com.example.game");
    CHECK(back[0].app_id == 10123);

    CHECK(armed_candidate(back, "com.example.game", 10123));
    CHECK(armed_candidate(back, "com.example.game:remote", 10123));
    CHECK(armed_candidate(back, "renamed.process", 10123 + 100000));   // secondary Android user, same app id
    CHECK(!armed_candidate(back, "com.android.chrome", 10999));        // normal apps never reach the companion
    CHECK(!armed_candidate(back, "com.android.settings", 1000));
    CHECK(!armed_candidate({}, "com.example.game", 10123));
    CHECK(armed_list_from_text("garbage\n\t\t\nno-tabs").empty());
}

// -- wire ---------------------------------------------------------------------------------------------------------------------

void test_decision_round_trip() {
    auto p = plan_for({Layer::Device, Layer::Gpu});
    auto v = validate_plan(p, good_env());
    Decision d;
    d.result = Decision::Result::Target;
    d.package = p.package;
    d.transaction_id = p.transaction_id;
    d.profile = p.profile;
    d.items = items_from_plan(p, v);
    d.layers = v.layers;
    Decision back;
    std::string err;
    CHECK(decision_from_json(decision_to_json(d), back, err));
    CHECK(back.result == Decision::Result::Target);
    CHECK_EQ(back.items.build.at("MODEL"), "FlagX");
    CHECK_EQ(back.items.gl.at("renderer"), "Acme GPU 9");
    CHECK(back.items.vk.vendor_id.value_or(0) == 0x1234);
    CHECK(back.items.vk.device_id.value_or(0) == 77);
    CHECK(back.items.vk.api_version.has_value());
    CHECK(!back.items.vk.driver_version.has_value());
    CHECK(back.layers.size() == 2);

    Decision rej;
    rej.result = Decision::Result::Rejected;
    rej.reject = Reject::Expired;
    Decision rb;
    CHECK(decision_from_json(decision_to_json(rej), rb, err));
    CHECK(rb.result == Decision::Result::Rejected && rb.reject == Reject::Expired);
    CHECK(!decision_from_json("{", rb, err));
}

void test_status_round_trip() {
    ProcStatus s;
    s.package = "com.example.game";
    s.process = "com.example.game";
    s.transaction_id = "tx";
    s.pid = 999;
    s.uid = 10123;
    s.state = ProcState::Verified;
    s.layers = {{Layer::Device, "verified", ""}, {Layer::Gpu, "installed", "waiting for first GL call"}};
    ProcStatus b;
    std::string err;
    CHECK(proc_status_from_json(proc_status_to_json(s), b, err));
    CHECK(b.state == ProcState::Verified && b.pid == 999);
    CHECK(b.layers.size() == 2 && b.layers[1].state == "installed");

    ProviderInfo pi{true, 5, "boot", 77, 123456}, pb;
    CHECK(provider_info_from_json(provider_info_to_json(pi), pb, err));
    CHECK(pb.loaded && pb.api_version == 5 && pb.boot_id == "boot");
}

} // namespace

int main() {
    test_layer_selection_is_exactly_the_plan();
    test_display_layer_never_reaches_the_provider();
    test_all_three_layers();
    test_unknown_game_and_mode_real_produce_inert_plans();
    test_transaction_validation();
    test_expired_plan();
    test_stale_plan_from_another_boot_or_dead_daemon();
    test_invalid_plans();
    test_plan_json_round_trip_and_legacy_fields();
    test_unsupported_isa_feature_rejected();
    test_unsupported_vulkan_capability_rejected();
    test_unknown_field_and_bad_values();
    test_one_bad_layer_does_not_apply_but_others_do();
    test_vulkan_api_version_never_above_real();
    test_package_and_uid_matching();
    test_multi_process_scope();
    test_profile_selection_by_package();
    test_armed_list_prefilter();
    test_decision_round_trip();
    test_status_round_trip();
    return flux_test::report("provider_test");
}
