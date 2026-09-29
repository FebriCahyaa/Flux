// Host tests for the Flux compatibility engine (jni/compat).
// Nothing here touches a device path: Io is a map, hardware is a struct.

#include <map>
#include <set>

#include "flux_test.hpp"

#include "Analyze.hpp"
#include "CapabilityModel.hpp"
#include "GameRuntime.hpp"

using namespace flux::compat;

namespace {

struct FakeFs {
    std::map<std::string, std::string> files;
    std::set<std::string> fail_writes;
    std::vector<std::string> writes;

    Io io() {
        Io i;
        i.exists = [this](const std::string &p) { return files.count(p) > 0; };
        i.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = files.find(p);
            if (it == files.end()) return std::nullopt;
            return it->second + "\n"; // sysfs nodes end in a newline
        };
        i.write = [this](const std::string &p, const std::string &v) {
            if (fail_writes.count(p)) return false;
            writes.push_back(p);
            files[p] = v;
            return true;
        };
        return i;
    }
};

RealHardware capable_hw() {
    RealHardware hw;
    hw.brand = "Real";
    hw.model = "RealPhone";
    hw.soc = "realsoc";
    hw.gpu_vendor = "Qualcomm";
    hw.gpu_model = "Adreno 650";
    hw.vulkan = Tri::Yes;
    hw.refresh_peak_hz = 120;
    hw.refresh_modes_hz = {60, 90, 119.99};
    hw.ram_mb = 8192;
    hw.sdk = 34;
    return hw;
}

ProfileLibrary library() {
    ProfileLibrary lib;
    std::string err;
    bool ok = lib.load_json(R"({
      "presets": {
        "competitive": {"performance": {"profile":"performance","touch":"competitive","storage":"gaming","launch_boost":true}},
        "child": {"extends":"competitive","performance":{"touch":"responsive"}}
      },
      "identities": {
        "flagship_device": {"layer":"device","fields":{"MODEL":"FlagshipX","BRAND":"Acme"}},
        "flagship_cpu":    {"layer":"cpu","fields":{"SOC_MODEL":"acme8"}},
        "flagship_gpu":    {"layer":"gpu","fields":{"renderer":"Acme GPU"}}
      }})", err);
    CHECK(ok);
    return lib;
}

EffectiveProfile eff(Mode m) {
    EffectiveProfile e;
    e.package = "com.example.game";
    e.mode = m;
    return e;
}

GameRequirement req(std::vector<GateKind> gates, double fps = 120) {
    GameRequirement r;
    r.package = "com.example.game";
    r.known = true;
    r.confidence = Confidence::High;
    r.target_fps = fps;
    for (auto g : gates) r.gates.push_back({g, "", ""});
    return r;
}

std::set<Layer> layers(const Resolution &r) { return r.required_layers(); }

// -- profiles -----------------------------------------------------------------

void test_profile_inheritance() {
    auto lib = library();
    std::string err;
    GameProfile global;
    global.perf.profile = "balance";
    global.perf.memory = "balanced";

    GameProfile game;
    game.package = "com.example.game";
    game.extends = "child";
    game.perf.storage = "balanced"; // overrides the preset's "gaming"

    auto e = lib.resolve("com.example.game", global, game, GameProfile{}, err);
    CHECK(err.empty());
    CHECK_EQ(e.performance, "performance");   // from the preset chain
    CHECK_EQ(e.touch, "responsive");          // child preset beats its parent
    CHECK_EQ(e.storage, "balanced");          // game beats the preset
    CHECK_EQ(e.memory, "balanced");           // global survives untouched
    CHECK(e.launch_boost);                    // inherited two levels up
    CHECK_EQ(e.device_profile, "real_device");
}

void test_profile_runtime_override_wins() {
    auto lib = library();
    std::string err;
    GameProfile game;
    game.perf.memory = "gaming";
    GameProfile runtime;
    runtime.perf.memory = "default";
    auto e = lib.resolve("p", GameProfile{}, game, runtime, err);
    CHECK_EQ(e.memory, "default");
}

void test_profile_cycle_and_unknown_preset() {
    ProfileLibrary lib;
    std::string err;
    CHECK(lib.load_json(R"({"presets":{"a":{"extends":"b"},"b":{"extends":"a"}}})", err));
    GameProfile game;
    game.extends = "a";
    lib.resolve("p", GameProfile{}, game, GameProfile{}, err);
    CHECK(err.find("cycle") != std::string::npos);

    err.clear();
    game.extends = "missing";
    lib.resolve("p", GameProfile{}, game, GameProfile{}, err);
    CHECK(err.find("unknown preset") != std::string::npos);
}

void test_invalid_profile_rejected() {
    GameProfile p;
    std::string err;
    CHECK(!parse_profile(R"({"performance":{"memory":"turbo"}})", p, err));
    CHECK(!parse_profile(R"({"compatibility":{"mode":"yolo"}})", p, err));
    CHECK(!parse_profile("not json", p, err));
    CHECK(!parse_profile(R"({"performance":{"launch_boost":"yes"}})", p, err));
    CHECK(parse_profile(R"({"package":"a.b","performance":{"memory":"gaming_plus"},"compatibility":{"mode":"auto"}})", p, err));
    CHECK_EQ(*p.perf.memory, "gaming_plus");

    // round-trip
    GameProfile again;
    CHECK(parse_profile(profile_to_json(p), again, err));
    CHECK_EQ(*again.perf.memory, "gaming_plus");
}

void test_identity_cannot_claim_capability() {
    ProfileLibrary lib;
    std::string err;
    CHECK(!lib.load_json(R"({"identities":{"bad_cpu":{"layer":"cpu","fields":{"hwcap_features":"sve2"}}}})", err));
    CHECK(!lib.load_json(R"({"identities":{"bad_gpu":{"layer":"gpu","fields":{"vulkan_extensions":"VK_X"}}}})", err));
    CHECK(!lib.load_json(R"({"identities":{"disp":{"layer":"display","fields":{}}}})", err));
    CHECK(lib.load_json(R"({"identities":{"ok":{"layer":"gpu","fields":{"renderer":"Acme"}}}})", err));
}

// -- resolver -------------------------------------------------------------------

void test_known_device_only_gate() {
    auto lib = library();
    auto e = eff(Mode::Auto);
    GameRequirement q = req({});
    q.gates = {{GateKind::DeviceIdentity, "", "flagship_device"}};
    auto r = resolve_compatibility(e, q, capable_hw(), lib);
    CHECK((layers(r) == std::set<Layer>{Layer::Device}));   // minimum: nothing else
    CHECK(r.unlocked == Tri::Yes);
    CHECK(r.capable == Tri::Yes);
    CHECK(r.sustained == Tri::Unknown);                      // never decided by the resolver
    CHECK(r.should_apply);
}

void test_auto_without_identity_does_not_guess() {
    auto lib = library();
    auto r = resolve_compatibility(eff(Mode::Auto), req({GateKind::DeviceIdentity}), capable_hw(), lib);
    CHECK(layers(r).empty()); // gate seen, but no identity to adopt: refuse to invent one
    CHECK(r.decision(Layer::Device).state == LayerState::Available);
    CHECK(!r.should_apply);
}

// Auto needs an identity to adopt: give the gate a curated one.
Resolution auto_with(std::vector<Gate> gates, const RealHardware &hw, double fps = 120) {
    auto lib = library();
    GameRequirement r = req({}, fps);
    r.gates = std::move(gates);
    return resolve_compatibility(eff(Mode::Auto), r, hw, lib);
}

void test_gate_combinations() {
    auto hw = capable_hw();
    auto d = auto_with({{GateKind::DeviceIdentity, "", "flagship_device"}}, hw);
    CHECK((layers(d) == std::set<Layer>{Layer::Device}));
    CHECK_EQ(d.decision(Layer::Device).identity, "flagship_device");

    auto c = auto_with({{GateKind::CpuIdentity, "", "flagship_cpu"}}, hw);
    CHECK((layers(c) == std::set<Layer>{Layer::Cpu}));

    auto g = auto_with({{GateKind::GpuIdentity, "", "flagship_gpu"}}, hw);
    CHECK((layers(g) == std::set<Layer>{Layer::Gpu}));

    auto dc = auto_with({{GateKind::DeviceIdentity, "", "flagship_device"}, {GateKind::CpuIdentity, "", "flagship_cpu"}}, hw);
    CHECK((layers(dc) == std::set<Layer>{Layer::Device, Layer::Cpu}));

    auto dg = auto_with({{GateKind::DeviceIdentity, "", "flagship_device"}, {GateKind::GpuIdentity, "", "flagship_gpu"}}, hw);
    CHECK((layers(dg) == std::set<Layer>{Layer::Device, Layer::Gpu}));

    auto all = auto_with({{GateKind::DeviceIdentity, "", "flagship_device"}, {GateKind::CpuIdentity, "", "flagship_cpu"},
                          {GateKind::GpuIdentity, "", "flagship_gpu"}}, hw);
    CHECK((layers(all) == std::set<Layer>{Layer::Device, Layer::Cpu, Layer::Gpu}));
    CHECK(all.unlocked == Tri::Yes);
}

void test_refresh_gate() {
    auto r = auto_with({{GateKind::DisplayRefresh, "", ""}}, capable_hw());
    CHECK((layers(r) == std::set<Layer>{Layer::Display}));
    CHECK(r.decision(Layer::Display).state == LayerState::Available);
    CHECK(r.capable == Tri::Yes);

    // A 60 Hz panel cannot be helped by any compatibility layer.
    auto hw = capable_hw();
    hw.refresh_peak_hz = 60;
    hw.refresh_modes_hz = {60};
    auto low = auto_with({{GateKind::DisplayRefresh, "", ""}}, hw);
    CHECK(layers(low).empty());
    CHECK(low.capable == Tri::No);
    CHECK(!low.should_apply);
    CHECK(!low.blockers.empty());
}

void test_no_gate_means_no_spoof() {
    auto lib = library();
    auto r = resolve_compatibility(eff(Mode::Auto), req({GateKind::None}), capable_hw(), lib);
    CHECK(layers(r).empty());
    CHECK(!r.should_apply);
    CHECK(r.recommendation.empty()); // analysed and clean, nothing to create
}

void test_unknown_game() {
    auto lib = library();
    auto r = resolve_compatibility(eff(Mode::Auto), std::nullopt, capable_hw(), lib);
    CHECK(!r.known_game);
    CHECK(r.confidence == Confidence::Unknown);
    CHECK(layers(r).empty());
    CHECK(!r.should_apply);
    CHECK_EQ(r.recommendation, "create_profile");
    CHECK(r.unlocked == Tri::Unknown);
    for (const auto &d : r.layers) CHECK(d.state == LayerState::Unknown);
}

void test_unfixable_gates_reported_honestly() {
    auto lib = library();
    for (GateKind k : {GateKind::ServerControlled, GateKind::Entitlement, GateKind::HardwareInsufficient}) {
        auto r = resolve_compatibility(eff(Mode::Auto), req({k}), capable_hw(), lib);
        CHECK(layers(r).empty());
        CHECK(!r.blockers.empty());
        CHECK(r.unlocked != Tri::Yes);
    }
}

void test_real_mode_never_applies() {
    auto lib = library();
    auto r = resolve_compatibility(eff(Mode::Real), req({GateKind::DeviceIdentity}), capable_hw(), lib);
    CHECK(!r.should_apply);
    CHECK(layers(r).empty());
}

void test_capability_from_hardware_not_identity() {
    // Unlocked without capable: the identity gate is satisfiable but the GPU is not
    // there. Auto refuses; an explicit mode applies with a warning.
    auto hw = capable_hw();
    hw.vulkan = Tri::No;
    GameRequirement r = req({}, 0);
    r.needs_vulkan = true;
    r.gates = {{GateKind::GpuIdentity, "", "flagship_gpu"}};
    auto lib = library();

    auto autoplan = resolve_compatibility(eff(Mode::Auto), r, hw, lib);
    CHECK(autoplan.capable == Tri::No);
    CHECK(!autoplan.should_apply);
    CHECK(autoplan.decision(Layer::Gpu).state == LayerState::Unsupported);

    auto e = eff(Mode::Compatibility);
    e.gpu_profile = "flagship_gpu";
    auto explicit_plan = resolve_compatibility(e, r, hw, lib);
    CHECK(explicit_plan.should_apply);
    CHECK(explicit_plan.capable == Tri::No);
    CHECK(!explicit_plan.warnings.empty());
}

void test_advanced_mode_uses_user_layers() {
    auto lib = library();
    auto e = eff(Mode::Advanced);
    e.device_profile = "flagship_device";
    e.gpu_profile = "flagship_gpu";
    auto r = resolve_compatibility(e, std::nullopt, capable_hw(), lib);
    CHECK((layers(r) == std::set<Layer>{Layer::Device, Layer::Gpu}));
}

void test_missing_or_wrong_layer_identity() {
    auto lib = library();
    auto e = eff(Mode::Advanced);
    e.device_profile = "does_not_exist";
    e.cpu_profile = "flagship_gpu"; // GPU identity on the CPU layer
    auto r = resolve_compatibility(e, std::nullopt, capable_hw(), lib);
    CHECK(layers(r).empty());
    CHECK(r.decision(Layer::Device).state == LayerState::Unsupported);
    CHECK(r.decision(Layer::Cpu).state == LayerState::Unsupported);
}

void test_sustained_is_measured() {
    CHECK(judge_sustained({87, 120, true}) == Tri::No);
    CHECK(judge_sustained({119, 120, true}) == Tri::Yes);
    CHECK(judge_sustained({0, 120, false}) == Tri::Unknown);
    CHECK(judge_sustained({60, 0, true}) == Tri::Unknown);
}

void test_known_db() {
    KnownGameDb db;
    std::string err;
    CHECK(db.load_json(R"({"games":{"a.b":{"target_fps":120,"gates":[{"kind":"device_identity","identity":"flagship_device"}]}}})", err));
    CHECK(db.find("a.b").has_value());
    CHECK(!db.find("x.y").has_value());
    CHECK(!db.load_json(R"({"games":{"a.b":{"gates":["bogus"]}}})", err));
    CHECK(db.size() == 1); // failed load leaves the previous data intact
}

// -- runtime -------------------------------------------------------------------

RuntimeDeps deps(FakeFs &fs, ProfileLibrary &lib, NativeBackend &nb, Backend *zb = nullptr) {
    RuntimeDeps d;
    d.io = fs.io();
    d.native = &nb;
    d.zygisk = zb;
    d.library = &lib;
    d.block_queues = {"/sys/block/sda/queue"};
    return d;
}

void seed_perf_nodes(FakeFs &fs) {
    fs.files["/proc/sys/vm/swappiness"] = "100";
    fs.files["/proc/sys/vm/vfs_cache_pressure"] = "120";
    fs.files["/proc/sys/vm/page-cluster"] = "3";
    fs.files["/sys/block/sda/queue/read_ahead_kb"] = "128";
    fs.files["/sys/block/sda/queue/rq_affinity"] = "1";
}

void test_perf_apply_and_restore() {
    FakeFs fs;
    seed_perf_nodes(fs);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));

    auto e = eff(Mode::Real);
    e.memory = "gaming";
    e.storage = "gaming";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.perf_context == ContextState::Active);
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "60");
    CHECK_EQ(fs.files["/sys/block/sda/queue/read_ahead_kb"], "512");
    CHECK_EQ(fs.files["/sys/block/sda/queue/rq_affinity"], "2");
    CHECK(!rt.journal().empty());

    CHECK(rt.deactivate());
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "100");
    CHECK_EQ(fs.files["/proc/sys/vm/vfs_cache_pressure"], "120");
    CHECK_EQ(fs.files["/sys/block/sda/queue/read_ahead_kb"], "128");
    CHECK(!rt.active());
}

void test_unsupported_nodes_never_written() {
    FakeFs fs; // no nodes at all
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto e = eff(Mode::Real);
    e.memory = "gaming_plus";
    e.touch = "competitive";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(fs.writes.empty());
    CHECK(a.perf_context == ContextState::Inactive);
    bool saw_memory = false;
    for (const auto &c : a.perf)
        if (c.category == "memory") { saw_memory = true; CHECK(c.support == Support::Unsupported); }
    CHECK(saw_memory);
}

void test_default_touches_nothing() {
    FakeFs fs;
    seed_perf_nodes(fs);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto a = rt.activate(eff(Mode::Real), std::nullopt, capable_hw());
    CHECK(fs.writes.empty());
    for (const auto &c : a.perf) CHECK(c.support == Support::Auto);
}

void test_apply_failure_rolls_back() {
    FakeFs fs;
    seed_perf_nodes(fs);
    fs.fail_writes.insert("/proc/sys/vm/page-cluster"); // third write of "gaming" fails
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto e = eff(Mode::Real);
    e.memory = "gaming";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.perf_context == ContextState::Failed);
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "100"); // already-applied write undone
    CHECK_EQ(fs.files["/proc/sys/vm/vfs_cache_pressure"], "120");
    CHECK(!rt.active());
}

void test_verification_failure_rolls_back() {
    // The write "succeeds" but the node does not hold the value (kernel clamped it).
    FakeFs fs;
    seed_perf_nodes(fs);
    auto io = fs.io();
    io.write = [&fs](const std::string &p, const std::string &v) {
        fs.writes.push_back(p);
        fs.files[p] = (p == "/proc/sys/vm/vfs_cache_pressure" && v == "80") ? "100" : v;
        return true;
    };
    auto lib = library();
    NativeBackend nb;
    RuntimeDeps d = deps(fs, lib, nb);
    d.io = io;
    GameRuntime rt(d);
    auto e = eff(Mode::Real);
    e.memory = "gaming";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.perf_context == ContextState::Failed);
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "100");
    CHECK_EQ(fs.files["/proc/sys/vm/vfs_cache_pressure"], "120");
}

void test_restore_failure_is_reported() {
    FakeFs fs;
    seed_perf_nodes(fs);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto e = eff(Mode::Real);
    e.memory = "balanced";
    rt.activate(e, std::nullopt, capable_hw());
    fs.fail_writes.insert("/proc/sys/vm/vfs_cache_pressure"); // restore will fail
    CHECK(!rt.deactivate());
}

void test_mitigation_blocks_category() {
    FakeFs fs;
    seed_perf_nodes(fs);
    auto lib = library();
    NativeBackend nb;
    RuntimeDeps d = deps(fs, lib, nb);
    d.mitigation_allows = [](const std::string &c) { return c != "memory"; };
    GameRuntime rt(d);
    auto e = eff(Mode::Real);
    e.memory = "gaming";
    e.storage = "gaming";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "100");         // blocked
    CHECK_EQ(fs.files["/sys/block/sda/queue/read_ahead_kb"], "512"); // allowed
    bool logged = false;
    for (const auto &l : a.log) logged = logged || l.find("skipped by device mitigation") != std::string::npos;
    CHECK(logged);
    rt.deactivate();
}

void test_scheduler_snapshot_view() {
    FakeFs fs;
    fs.files["/sys/block/sda/queue/scheduler"] = "[mq-deadline] none";
    auto view = [](const std::string &raw) {
        size_t a = raw.find('['), b = raw.find(']');
        return (a != std::string::npos && b != std::string::npos) ? raw.substr(a + 1, b - a - 1) : raw;
    };
    Transaction tx("p");
    tx.add(std::make_unique<NodeWrite>(fs.io(), "storage", "/sys/block/sda/queue/scheduler", "none", view));
    // the fake write replaces the whole text, as sysfs would report a plain name
    CHECK(tx.start());
    CHECK_EQ(fs.files["/sys/block/sda/queue/scheduler"], "none");
}

// -- backends ------------------------------------------------------------------

void test_native_backend_refuses_identity() {
    FakeFs fs;
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto e = eff(Mode::Advanced);
    e.device_profile = "flagship_device";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.context == ContextState::Failed);
    CHECK(a.backend_state == BackendState::Unavailable);
    CHECK(a.resolution.decision(Layer::Device).state == LayerState::Failed);
    CHECK(!a.resolution.should_apply);
    CHECK(fs.writes.empty()); // nothing system-wide, ever
}

void test_zygisk_states() {
    FakeFs fs;
    ZygiskBackend::Config cfg;
    CHECK(ZygiskBackend(fs.io(), cfg, 34).available() == BackendState::Unavailable);   // no provider
    fs.files["/data/adb/modules/rezygisk"] = "";
    CHECK(ZygiskBackend(fs.io(), cfg, 34).available() == BackendState::NotConfigured); // present, not opted in
    cfg.user_enabled = true;
    CHECK(ZygiskBackend(fs.io(), cfg, 34).available() == BackendState::Available);
    CHECK(ZygiskBackend(fs.io(), cfg, 21).available() == BackendState::Unsupported);   // too old
}

void test_zygisk_apply_verify_restore() {
    FakeFs fs;
    fs.files["/data/adb/modules/rezygisk"] = "";
    ZygiskBackend::Config cfg;
    cfg.user_enabled = true;
    ZygiskBackend zb(fs.io(), cfg, 34);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb, &zb));

    auto e = eff(Mode::Advanced);
    e.device_profile = "flagship_device";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.context == ContextState::Active);
    CHECK_EQ(a.backend, "zygisk");
    CHECK(a.resolution.decision(Layer::Device).state == LayerState::Verified);
    const std::string spool = "/data/adb/flux/compat/zygisk/com.example.game.json";
    CHECK(fs.files[spool].find("FlagshipX") != std::string::npos);
    // Two views: real hardware stays real, the game gets the profile.
    CHECK_EQ(a.effective_identity["device"], "flagship_device");

    CHECK(rt.deactivate());
    CHECK_EQ(fs.files[spool], "{\"active\":false}");
}

void test_zygisk_only_scoped_to_target_package() {
    FakeFs fs;
    fs.files["/data/adb/modules/rezygisk"] = "";
    ZygiskBackend::Config cfg;
    cfg.user_enabled = true;
    ZygiskBackend zb(fs.io(), cfg, 34);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb, &zb));
    auto e = eff(Mode::Advanced);
    e.device_profile = "flagship_device";
    rt.activate(e, std::nullopt, capable_hw());
    for (const auto &w : fs.writes) CHECK(w.find("com.example.game") != std::string::npos);
    CHECK(!zb.supports("../etc/x"));
    CHECK(!zb.supports(""));
    rt.deactivate();
}

void test_backend_failure_keeps_performance() {
    FakeFs fs;
    seed_perf_nodes(fs);
    fs.files["/data/adb/modules/rezygisk"] = "";
    fs.fail_writes.insert("/data/adb/flux/compat/zygisk/com.example.game.json");
    ZygiskBackend::Config cfg;
    cfg.user_enabled = true;
    ZygiskBackend zb(fs.io(), cfg, 34);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb, &zb));
    auto e = eff(Mode::Advanced);
    e.device_profile = "flagship_device";
    e.memory = "gaming";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.context == ContextState::Failed);
    CHECK(a.perf_context == ContextState::Active);           // performance is independent
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "60");
    CHECK(rt.deactivate());
}

// -- refresh ---------------------------------------------------------------------

void test_refresh_request_is_honest() {
    FakeFs fs;
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto e = eff(Mode::Real);
    e.refresh = "hz120";
    auto a = rt.activate(e, std::nullopt, capable_hw());
    CHECK(a.refresh_request_hz == 120);
    rt.deactivate();

    auto hw = capable_hw();
    hw.refresh_peak_hz = 60;
    hw.refresh_modes_hz = {60};
    auto b = rt.activate(e, std::nullopt, hw);
    CHECK(b.refresh_request_hz == 0);
    CHECK(!b.refresh_note.empty());
    rt.deactivate();

    e.refresh = "adaptive";
    auto c = rt.activate(e, std::nullopt, capable_hw());
    CHECK(c.refresh_request_hz == 0); // adaptive stays with the existing implementation
    rt.deactivate();
}

// -- launch boost ---------------------------------------------------------------

void test_launch_boost_bounded_and_cancelable() {
    FakeFs fs;
    fs.files["/sys/block/sda/queue/read_ahead_kb"] = "128";
    fs.files["/dev/cpuctl/top-app/cpu.uclamp.min"] = "0";
    LaunchBoost lb(fs.io(), {"/sys/block/sda/queue"}, 5000);
    CHECK(lb.begin(1000));
    CHECK_EQ(fs.files["/sys/block/sda/queue/read_ahead_kb"], "1024");
    CHECK(lb.tick(3000));
    CHECK(!lb.begin(3100)); // no stacking
    CHECK(!lb.tick(6000));  // deadline passed -> auto cancel
    CHECK(lb.phase() == LaunchBoost::Phase::Done);
    CHECK_EQ(lb.last_cancel(), "timeout");
    CHECK_EQ(fs.files["/sys/block/sda/queue/read_ahead_kb"], "128");
    CHECK_EQ(fs.files["/dev/cpuctl/top-app/cpu.uclamp.min"], "0");
}

void test_launch_boost_cancel_on_main_active() {
    FakeFs fs;
    fs.files["/dev/cpuctl/top-app/cpu.uclamp.min"] = "0";
    LaunchBoost lb(fs.io(), {}, 5000);
    CHECK(lb.begin(0));
    CHECK(lb.cancel(LaunchBoost::Cancel::MainActive));
    CHECK_EQ(fs.files["/dev/cpuctl/top-app/cpu.uclamp.min"], "0");
    CHECK(lb.cancel(LaunchBoost::Cancel::ProcessExit)); // idempotent
}

void test_launch_boost_without_nodes_is_noop() {
    FakeFs fs;
    LaunchBoost lb(fs.io(), {"/sys/block/sda/queue"});
    CHECK(!lb.begin(0));
    CHECK(fs.writes.empty());
}

// -- watchdog -------------------------------------------------------------------

void test_watchdog_recovers_after_crash() {
    FakeFs fs;
    seed_perf_nodes(fs);
    auto lib = library();
    NativeBackend nb;
    std::vector<std::string> journal;
    {
        GameRuntime rt(deps(fs, lib, nb));
        auto e = eff(Mode::Real);
        e.memory = "gaming";
        rt.activate(e, std::nullopt, capable_hw());
        journal = rt.journal();
        // "crash": the runtime is dropped without deactivate()
    }
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "60");
    CHECK(!journal.empty());
    size_t n = Watchdog::recover(fs.io(), journal);
    CHECK(n == journal.size());
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "100");
    CHECK_EQ(fs.files["/proc/sys/vm/page-cluster"], "3");
}

void test_watchdog_rejects_hostile_journal() {
    FakeFs fs;
    CHECK(!Watchdog::recover_line(fs.io(), "no tab here"));
    CHECK(!Watchdog::recover_line(fs.io(), "../../etc/passwd\tx"));
    CHECK(!Watchdog::recover_line(fs.io(), "relative/path\tx"));
    CHECK(fs.writes.empty());
    CHECK(Watchdog::must_restore(ContextState::Active, false));
    CHECK(!Watchdog::must_restore(ContextState::Active, true));
    CHECK(!Watchdog::must_restore(ContextState::Restored, false));
}

void test_activation_json_is_valid() {
    FakeFs fs;
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto a = rt.activate(eff(Mode::Auto), std::nullopt, capable_hw());
    std::string js = a.to_json();
    CHECK(js.find("\"recommendation\":\"create_profile\"") != std::string::npos);
    CHECK(js.find("\"sustained\":\"unknown\"") != std::string::npos);
    CHECK(js.find("effective_identity") != std::string::npos);
    rt.deactivate();
}

void test_second_activation_restores_first() {
    FakeFs fs;
    seed_perf_nodes(fs);
    auto lib = library();
    NativeBackend nb;
    GameRuntime rt(deps(fs, lib, nb));
    auto e = eff(Mode::Real);
    e.memory = "gaming";
    rt.activate(e, std::nullopt, capable_hw());
    e.memory = "balanced";
    rt.activate(e, std::nullopt, capable_hw());
    rt.deactivate();
    CHECK_EQ(fs.files["/proc/sys/vm/swappiness"], "100"); // never left at the first game's value
}

void test_hardware_from_capability_model() {
    namespace sc = flux::gfx::schema;
    flux::gfx::CapabilityModel m;
    m.set(sc::domain::kVulkan, sc::vulkan::kAvailable, true, "t");
    m.set(sc::domain::kGpu, sc::gpu::kVendor, std::string("Qualcomm"), "t");
    m.set(sc::domain::kDisplay, sc::display::kPeakRefreshRateHz, 120.0, "t");
    m.set(sc::domain::kDisplay, sc::display::kSupportedModes,
          std::vector<std::string>{"1080x2400@60.0", "1080x2400@119.99"}, "t");
    m.set(sc::domain::kRuntime, sc::runtime::kAndroidSdk, int64_t{34}, "t");
    auto hw = hardware_from_model(m);
    CHECK(hw.vulkan == Tri::Yes);
    CHECK_EQ(hw.gpu_vendor, "Qualcomm");
    CHECK(hw.panel_supports(120) == Tri::Yes);
    CHECK(hw.panel_supports(144) == Tri::No);
    CHECK_EQ(hw.sdk, int64_t{34});
    CHECK(RealHardware{}.panel_supports(120) == Tri::Unknown); // never read -> not "no"
}

void test_analyze_unknown_and_known() {
    AnalyzeInputs in;
    in.library_json = R"({"identities":{"fx":{"layer":"device","fields":{"MODEL":"X"}}}})";
    in.known_games_json = R"({"games":{"k.game":{"target_fps":120,"gates":[{"kind":"device_identity","identity":"fx"}]}}})";

    auto unknown = analyze_package("new.game", std::nullopt, in);
    CHECK(unknown.find("\"ok\":true") != std::string::npos);
    CHECK(unknown.find("\"recommendation\":\"create_profile\"") != std::string::npos);
    CHECK(unknown.find("\"has_profile\":false") != std::string::npos);

    auto known = analyze_package("k.game", std::nullopt, in);
    CHECK(known.find("\"known_game\":true") != std::string::npos);
    CHECK(known.find("\"required\":true") != std::string::npos);

    // hostile / broken input is answered, never crashes
    CHECK(analyze_package("../x", std::nullopt, in).find("\"ok\":false") != std::string::npos);
    AnalyzeInputs bad = in;
    bad.capabilities_json = "{nope";
    CHECK(analyze_package("a.b", std::nullopt, bad).find("\"ok\":false") != std::string::npos);
    bad = in;
    bad.profiles_json = R"({"a.b":{"performance":{"memory":"turbo"}}})";
    CHECK(analyze_package("a.b", std::nullopt, bad).find("\"ok\":false") != std::string::npos);
}

} // namespace

int main() {
    test_hardware_from_capability_model();
    test_analyze_unknown_and_known();
    test_profile_inheritance();
    test_profile_runtime_override_wins();
    test_profile_cycle_and_unknown_preset();
    test_invalid_profile_rejected();
    test_identity_cannot_claim_capability();
    test_known_device_only_gate();
    test_auto_without_identity_does_not_guess();
    test_gate_combinations();
    test_refresh_gate();
    test_no_gate_means_no_spoof();
    test_unknown_game();
    test_unfixable_gates_reported_honestly();
    test_real_mode_never_applies();
    test_capability_from_hardware_not_identity();
    test_advanced_mode_uses_user_layers();
    test_missing_or_wrong_layer_identity();
    test_sustained_is_measured();
    test_known_db();
    test_perf_apply_and_restore();
    test_unsupported_nodes_never_written();
    test_default_touches_nothing();
    test_apply_failure_rolls_back();
    test_verification_failure_rolls_back();
    test_restore_failure_is_reported();
    test_mitigation_blocks_category();
    test_scheduler_snapshot_view();
    test_native_backend_refuses_identity();
    test_zygisk_states();
    test_zygisk_apply_verify_restore();
    test_zygisk_only_scoped_to_target_package();
    test_backend_failure_keeps_performance();
    test_refresh_request_is_honest();
    test_launch_boost_bounded_and_cancelable();
    test_launch_boost_cancel_on_main_active();
    test_launch_boost_without_nodes_is_noop();
    test_watchdog_recovers_after_crash();
    test_watchdog_rejects_hostile_journal();
    test_activation_json_is_valid();
    test_second_activation_restores_first();
    return flux_test::report("compat_test");
}
