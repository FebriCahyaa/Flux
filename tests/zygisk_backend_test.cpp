// Arming + ZygiskBackend + GameRuntime, with the provider's side simulated by files in a fake
// filesystem (the companion itself is exercised for real in fluxd_companion_test).
//
// The behaviours that matter here: nothing is called "active" without the provider's own report
// for THIS process and THIS plan, an armed plan is configuration (it survives a session), and a
// missing provider never costs the game its Flux performance profile.

#include <map>
#include <set>

#include "flux_test.hpp"

#include "Analyze.hpp"
#include "GameRuntime.hpp"
#include "Session.hpp"
#include "ZygiskBackend.hpp"

using namespace flux::compat;
using namespace flux::compat::provider;

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
            return it->second + "\n";
        };
        i.write = [this](const std::string &p, const std::string &v) {
            if (fail_writes.count(p)) return false;
            writes.push_back(p);
            files[p] = v;
            return true;
        };
        i.write_atomic = i.write;
        return i;
    }
};

constexpr const char *kPkg = "com.example.game";
constexpr const char *kBoot = "boot-abc";
constexpr int64_t kPid = 4321;
const std::string kLib = "/data/adb/modules/flux/zygisk/arm64-v8a.so";
const std::string kPlan = "/data/adb/.config/flux/compat_provider/plans/com.example.game.json";
const std::string kArmed = "/data/adb/modules/flux/armed.list";
const std::string kInfo = "/data/adb/.config/flux/compat_provider/provider.json";

struct Fixture {
    FakeFs fs;
    ArmingConfig cfg;
    bool alive = true;
    int64_t now = 1'000'000;
    std::unique_ptr<Arming> arming;
    ProfileLibrary lib;
    NativeBackend native;

    Fixture(bool installed = true, bool opted_in = true, int64_t sdk = 34) {
        std::string err;
        lib.load_json(R"({"identities":{
          "dev":{"layer":"device","fields":{"MODEL":"FlagX","BRAND":"Acme"}},
          "gpu":{"layer":"gpu","fields":{"gl_renderer":"Acme GPU 9","vk_device_name":"Acme GPU 9"}},
          "cpu":{"layer":"cpu","fields":{"SOC_MODEL":"acme8"}}}})", err);
        if (installed) fs.files[kLib] = "";
        cfg.user_enabled = opted_in;
        ArmingEnv env;
        env.now_ms = [this] { return now; };
        env.boot_id = [] { return std::string(kBoot); };
        env.daemon_pid = [] { return int64_t{777}; };
        env.pid_alive = [this](int64_t) { return alive; };
        env.app_id_of = [](const std::string &p) { return p == "com.example.game" ? 10123 : 10200; };
        arming = std::make_unique<Arming>(fs.io(), cfg, env, sdk);
    }

    Resolution resolution(const std::string &pkg, std::set<Layer> layers) {
        Resolution r;
        r.package = pkg;
        r.mode = Mode::Advanced;
        r.layers.resize(4);
        const char *ids[] = {"dev", "cpu", "gpu", ""};
        for (Layer l : {Layer::Device, Layer::Cpu, Layer::Gpu, Layer::Display}) {
            auto &d = r.layers[static_cast<size_t>(l)];
            d.layer = l;
            d.required = layers.count(l) > 0;
            if (d.required) d.identity = ids[static_cast<size_t>(l)];
        }
        r.should_apply = !layers.empty();
        return r;
    }

    std::string arm(const std::string &pkg, std::set<Layer> layers, ProcessScope scope = {}) {
        std::string err;
        return arming->arm(resolution(pkg, layers), lib, scope, "game", err);
    }

    void report(int64_t pid, const std::string &tx, ProcState st, std::vector<LayerResult> layers, const std::string &pkg = kPkg) {
        ProcStatus s;
        s.package = pkg;
        s.process = pkg;
        s.transaction_id = tx;
        s.pid = pid;
        s.state = st;
        s.layers = std::move(layers);
        fs.files["/data/adb/.config/flux/compat_provider/proc/" + std::to_string(pid) + ".json"] = proc_status_to_json(s);
    }

    void provider_loaded() {
        ProviderInfo pi{true, 5, kBoot, 99, 1};
        fs.files[kInfo] = provider_info_to_json(pi);
    }

    EffectiveProfile profile(std::set<Layer> layers) {
        EffectiveProfile e;
        e.package = kPkg;
        e.mode = Mode::Advanced;
        if (layers.count(Layer::Device)) e.device_profile = "dev";
        if (layers.count(Layer::Cpu)) e.cpu_profile = "cpu";
        if (layers.count(Layer::Gpu)) e.gpu_profile = "gpu";
        return e;
    }

    GameRuntime runtime(ZygiskBackend &zb) {
        RuntimeDeps d;
        d.io = fs.io();
        d.native = &native;
        d.zygisk = &zb;
        d.library = &lib;
        return GameRuntime(d);
    }
};

RealHardware hw() {
    RealHardware h;
    h.vulkan = Tri::Yes;
    h.refresh_peak_hz = 120;
    h.refresh_modes_hz = {60, 120};
    return h;
}

// ---------------------------------------------------------------------------------------------------

void test_states_installed_opted_in_and_loaded() {
    Fixture none(false, true);
    CHECK(none.arming->backend_state() == BackendState::Unavailable);   // library not in the module
    CHECK(none.arming->provider_state() == ProviderState::Unavailable);

    Fixture off(true, false);
    CHECK(off.arming->backend_state() == BackendState::NotConfigured);  // installed, but the user never opted in
    CHECK(off.arming->provider_state() == ProviderState::NotConfigured);

    Fixture on(true, true);
    CHECK(on.arming->backend_state() == BackendState::Available);
    CHECK(on.arming->provider_state() == ProviderState::Installed);     // installed is NOT loaded
    CHECK(!on.arming->loaded());
    on.provider_loaded();
    CHECK(on.arming->provider_state() == ProviderState::Loaded);

    // provider.json from another boot proves nothing about this one
    ProviderInfo old{true, 5, "some-earlier-boot", 99, 1};
    on.fs.files[kInfo] = provider_info_to_json(old);
    CHECK(on.arming->provider_state() == ProviderState::Installed);

    Fixture ancient(true, true, 21);
    CHECK(ancient.arming->backend_state() == BackendState::Unsupported);

    Fixture disabled(true, true);
    disabled.fs.files["/data/adb/modules/flux/disable"] = "";           // module switched off in the manager
    CHECK(disabled.arming->backend_state() == BackendState::Unavailable);
    // Another module named zygisk-something changes nothing.
    Fixture other(false, true);
    other.fs.files["/data/adb/modules/zygisksu/module.prop"] = "";
    CHECK(other.arming->backend_state() == BackendState::Unavailable);
}

void test_arming_writes_a_valid_plan_and_the_prefilter_list() {
    Fixture f;
    std::string tx = f.arm(kPkg, {Layer::Device, Layer::Gpu});
    CHECK(!tx.empty());

    Plan p;
    std::string err;
    CHECK(plan_from_json(f.fs.files[kPlan], p, err));
    Env env{f.now + 10, kBoot, true};
    CHECK(validate_plan(p, env).ok());
    CHECK((p.layers == std::vector<Layer>{Layer::Device, Layer::Gpu}));   // exactly the resolver's layers, no CPU
    CHECK_EQ(p.transaction_id, tx);
    CHECK_EQ(p.boot_id, kBoot);
    CHECK(p.daemon_pid == 777);

    auto list = armed_list_from_text(f.fs.files[kArmed]);
    CHECK(list.size() == 1 && list[0].package == kPkg && list[0].app_id == 10123 && list[0].transaction_id == tx);
}

void test_rearming_is_stable_and_a_changed_plan_gets_a_new_transaction() {
    Fixture f;
    std::string a = f.arm(kPkg, {Layer::Device});
    f.now += 3'600'000;                                    // renew the lease later
    CHECK_EQ(f.arm(kPkg, {Layer::Device}), a);             // a running process still matches
    CHECK(f.arm(kPkg, {Layer::Device, Layer::Gpu}) != a);  // the plan changed: a process holding the old one is stale
    f.lib.identities["dev"].fields["MODEL"] = "Other";
    CHECK(f.arm(kPkg, {Layer::Device, Layer::Gpu}) != a);
}

void test_disarm_and_no_layers() {
    Fixture f;
    f.arm(kPkg, {Layer::Device});
    f.arm("com.example.other", {Layer::Gpu});
    CHECK(armed_list_from_text(f.fs.files[kArmed]).size() == 2);

    // The profile changed to need nothing: the plan must not stay armed.
    CHECK(f.arm(kPkg, {}).empty());
    auto list = armed_list_from_text(f.fs.files[kArmed]);
    CHECK(list.size() == 1 && list[0].package == "com.example.other");
    Plan p;
    std::string err;
    plan_from_json(f.fs.files[kPlan], p, err);
    CHECK(!p.active);

    f.arming->disarm_all();
    CHECK(armed_list_from_text(f.fs.files[kArmed]).empty());
}

void test_arming_refused_without_provider_or_opt_in() {
    Fixture off(true, false);
    std::string err;
    CHECK(off.arming->arm(off.resolution(kPkg, {Layer::Device}), off.lib, {}, "game", err).empty());
    CHECK(!err.empty());
    CHECK(off.fs.files.count(kPlan) == 0);      // nothing armed: nothing for a process to pick up
    CHECK(off.fs.files.count(kArmed) == 0);

    Fixture none(false, true);
    CHECK(none.arming->arm(none.resolution(kPkg, {Layer::Device}), none.lib, {}, "game", err).empty());
    CHECK(none.fs.files.count(kPlan) == 0);
}

// ---- what the backend believes ---------------------------------------------------------------------------

void test_verified_process_makes_the_context_active_and_the_plan_outlives_the_session() {
    Fixture f;
    f.provider_loaded();
    std::string tx = f.arm(kPkg, {Layer::Device});         // armed before launch, as the daemon does
    f.report(kPid, tx, ProcState::Verified, {{Layer::Device, "verified", "Build fields read back"}});

    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Active);
    CHECK_EQ(a.backend, "zygisk");
    CHECK_EQ(a.provider.state, "verified");
    CHECK_EQ(a.provider.transaction_id, tx);
    CHECK(a.resolution.decision(Layer::Device).state == LayerState::Verified);
    CHECK(a.resolution.decision(Layer::Cpu).state != LayerState::Verified);    // CPU layer was never required
    CHECK_EQ(a.effective_identity["device"], "dev");

    size_t before = f.fs.writes.size();
    CHECK(rt.deactivate());
    CHECK(f.fs.writes.size() == before);                   // ending a session writes nothing: no spool to clear
    CHECK(f.arming->transaction_of(kPkg) == tx);           // still armed for the next launch
    CHECK(armed_list_from_text(f.fs.files[kArmed]).size() == 1);
}

void test_installed_hooks_are_applied_not_verified() {
    Fixture f;
    f.provider_loaded();
    std::string tx = f.arm(kPkg, {Layer::Gpu});
    f.report(kPid, tx, ProcState::Applied, {{Layer::Gpu, "installed", "gl/egl slots=2"}});
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Gpu}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Active);
    CHECK_EQ(a.provider.state, "applied");
    CHECK(a.resolution.decision(Layer::Gpu).state == LayerState::Applied);    // NOT verified until a query is observed
    rt.deactivate();

    f.report(kPid, tx, ProcState::Verified, {{Layer::Gpu, "observed", "gl"}});
    auto b = rt.activate_compat(f.profile({Layer::Gpu}), std::nullopt, hw(), kPid, 10123);
    CHECK(b.resolution.decision(Layer::Gpu).state == LayerState::Verified);   // the game really asked and got the answer
    rt.deactivate();
}

void test_process_that_predates_the_plan_is_not_reported_active() {
    Fixture f;
    f.provider_loaded();                                   // provider works, this process just started before arming
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Failed);
    CHECK(a.resolution.decision(Layer::Device).state == LayerState::Failed);
    CHECK(zb.last_error().find("relaunch") != std::string::npos);
    CHECK(!a.effective_identity.count("device") || a.effective_identity["device"] != "dev");
    // But the plan IS armed now, for the next launch.
    CHECK(!f.arming->transaction_of(kPkg).empty());
    CHECK(f.fs.files.count(kPlan) == 1);
    rt.deactivate();
}

void test_provider_never_loaded_is_reported_as_such() {
    Fixture f;                                             // installed + opted in, but Zygisk never ran it
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Failed);
    CHECK(zb.last_error().find("not loaded") != std::string::npos || zb.last_error().find("has not loaded") != std::string::npos);
    CHECK_EQ(a.provider.state, "installed");               // not "active", not "loaded"
    rt.deactivate();
}

void test_stale_transaction_is_detected() {
    Fixture f;
    f.provider_loaded();
    f.arm(kPkg, {Layer::Device});
    f.report(kPid, "old-transaction", ProcState::Verified, {{Layer::Device, "verified", ""}});   // process runs an older plan
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Failed);
    CHECK(zb.last_error().find("older profile") != std::string::npos);
    rt.deactivate();
}

void test_provider_refusal_and_dead_process_are_failures() {
    Fixture f;
    f.provider_loaded();
    std::string tx = f.arm(kPkg, {Layer::Device});
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);

    f.report(kPid, tx, ProcState::Failed, {{Layer::Device, "failed", "expired: lease expired"}});
    auto a = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Failed);
    CHECK(zb.last_error().find("lease expired") != std::string::npos);
    rt.deactivate();

    f.report(kPid, tx, ProcState::Matched, {{Layer::Device, "matched", ""}});
    auto m = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(m.context == ContextState::Failed);               // matched is not applied
    rt.deactivate();

    f.report(kPid, tx, ProcState::Ended, {{Layer::Device, "verified", ""}});
    auto e = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(e.context == ContextState::Failed);
    rt.deactivate();

    f.alive = false;                                        // status file of a process that no longer exists
    f.report(kPid, tx, ProcState::Verified, {{Layer::Device, "verified", ""}});
    auto d = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(d.context == ContextState::Failed);
    rt.deactivate();
}

void test_status_of_another_process_is_ignored() {
    Fixture f;
    f.provider_loaded();
    std::string tx = f.arm(kPkg, {Layer::Device});
    f.report(kPid, tx, ProcState::Verified, {{Layer::Device, "verified", ""}});
    // a file named for kPid that actually describes another pid (pid reuse / copy)
    auto txt = f.fs.files["/data/adb/.config/flux/compat_provider/proc/4321.json"];
    ProcStatus s;
    std::string err;
    proc_status_from_json(txt, s, err);
    s.pid = 9999;
    f.fs.files["/data/adb/.config/flux/compat_provider/proc/4321.json"] = proc_status_to_json(s);
    CHECK(!f.arming->process_status(kPid).has_value());
}

void test_layer_failure_reported_by_the_provider() {
    Fixture f;
    f.provider_loaded();
    std::string tx = f.arm(kPkg, {Layer::Device, Layer::Cpu});
    f.report(kPid, tx, ProcState::Applied,
             {{Layer::Device, "verified", ""}, {Layer::Cpu, "unsupported", "SOC_MODEL does not exist on this Android version"}});
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Device, Layer::Cpu}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Active);
    CHECK(a.resolution.decision(Layer::Device).state == LayerState::Verified);
    CHECK(a.resolution.decision(Layer::Cpu).state == LayerState::Unsupported);   // reported, not fabricated
    rt.deactivate();
}

// ---- isolation from performance -------------------------------------------------------------------------------------

void test_arming_failure_keeps_performance() {
    Fixture f;
    f.fs.files["/proc/sys/vm/swappiness"] = "100";
    f.fs.files["/proc/sys/vm/vfs_cache_pressure"] = "120";
    f.fs.files["/proc/sys/vm/page-cluster"] = "3";
    f.fs.files["/proc/sys/vm/dirty_expire_centisecs"] = "3000";
    f.fs.files["/proc/sys/vm/watermark_boost_factor"] = "15000";
    f.fs.fail_writes.insert(kPlan);
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto prof = f.profile({Layer::Device});
    prof.memory = "gaming";
    auto a = rt.activate_compat(prof, std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Failed);
    rt.activate_perf(a);
    CHECK(a.perf_context == ContextState::Active);          // Flux performance continues
    CHECK_EQ(f.fs.files["/proc/sys/vm/swappiness"], "60");
    CHECK(rt.deactivate());
    CHECK_EQ(f.fs.files["/proc/sys/vm/swappiness"], "100");
}

void test_no_provider_reports_unavailable_and_writes_nothing() {
    Fixture f(false, true);
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    auto a = rt.activate_compat(f.profile({Layer::Device}), std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Failed);
    CHECK(a.backend_state == BackendState::Unavailable);
    CHECK_EQ(a.provider.state, "unavailable");
    CHECK(f.fs.writes.empty());
    CHECK(a.effective_identity["device"] != "dev");
    rt.deactivate();
}

void test_real_mode_and_unknown_game_arm_nothing() {
    Fixture f;
    f.provider_loaded();
    ZygiskBackend zb(*f.arming);
    auto rt = f.runtime(zb);
    EffectiveProfile real;
    real.package = kPkg;                                    // mode real, every profile "real_*"
    auto a = rt.activate_compat(real, std::nullopt, hw(), kPid, 10123);
    CHECK(a.context == ContextState::Inactive);
    CHECK(f.fs.files.count(kPlan) == 0 && f.fs.files.count(kArmed) == 0);

    EffectiveProfile unknown = real;
    unknown.mode = Mode::Auto;                              // unknown game: no evidence
    auto b = rt.activate_compat(unknown, std::nullopt, hw(), kPid, 10123);
    CHECK(b.context == ContextState::Inactive);
    CHECK(f.fs.files.count(kPlan) == 0);
    rt.deactivate();
}

void test_supports_rejects_hostile_package_names() {
    Fixture f;
    ZygiskBackend zb(*f.arming);
    CHECK(zb.supports("com.example.game"));
    CHECK(!zb.supports(""));
    CHECK(!zb.supports("../../etc/passwd"));
    CHECK(!zb.supports("a/b"));
}

void test_process_scope_reaches_the_plan() {
    Fixture f;
    f.provider_loaded();
    auto prof = f.profile({Layer::Device});
    prof.process_scope = "listed";
    prof.processes = {std::string(kPkg) + ":engine"};
    std::string tx = f.arm(kPkg, {Layer::Device}, ProcessScope{ProcessScope::Kind::Listed, prof.processes});
    Plan p;
    std::string err;
    CHECK(plan_from_json(f.fs.files[kPlan], p, err));
    CHECK(p.scope.kind == ProcessScope::Kind::Listed);
    CHECK(p.scope.processes.size() == 1);
    CHECK(!match_process(p, {kPkg, kPkg, 10123}).matched);                        // main process untouched
    CHECK(match_process(p, {std::string(kPkg) + ":engine", kPkg, 10123}).matched);
    (void)tx;

    // and through the profile parser
    GameProfile gp;
    CHECK(parse_profile(R"({"compatibility":{"mode":"advanced","process_scope":"listed","processes":["a.b:engine"]}})", gp, err));
    CHECK(*gp.compat.process_scope == "listed");
    CHECK(!parse_profile(R"({"compatibility":{"process_scope":"everything"}})", gp, err));
    CHECK(!parse_profile(R"({"compatibility":{"processes":[""]}})", gp, err));
}

// ---- arm_all: the daemon decides what is armed from the profiles --------------------------------------------------------

AnalyzeInputs docs() {
    AnalyzeInputs in;
    in.library_json = R"({"identities":{
      "dev":{"layer":"device","fields":{"MODEL":"FlagX"}},
      "gpu":{"layer":"gpu","fields":{"gl_renderer":"Acme GPU 9"}}}})";
    in.profiles_json = R"({
      "com.example.game":  {"compatibility":{"mode":"advanced","device_profile":"dev"}},
      "com.example.two":   {"compatibility":{"mode":"advanced","device_profile":"dev","gpu_profile":"gpu","process_scope":"all"}},
      "com.example.plain": {"performance":{"memory":"gaming"},"compatibility":{"mode":"real"}},
      "com.example.auto":  {"compatibility":{"mode":"auto"}},
      "../bad":            {"compatibility":{"mode":"advanced","device_profile":"dev"}}
    })";
    return in;
}

void test_arm_all_arms_only_what_the_resolver_requires() {
    Fixture f;
    f.provider_loaded();
    auto rep = arm_all(*f.arming, docs(), false);
    CHECK(rep.armed.size() == 2);                               // game + two
    auto list = armed_list_from_text(f.fs.files[kArmed]);
    std::set<std::string> pk;
    for (auto &e : list) pk.insert(e.package);
    CHECK((pk == std::set<std::string>{"com.example.game", "com.example.two"}));
    CHECK(f.fs.files.count("/data/adb/.config/flux/compat_provider/plans/com.example.plain.json") == 0);  // mode real: no plan
    CHECK(f.fs.files.count("/data/adb/.config/flux/compat_provider/plans/com.example.auto.json") == 0);   // unknown game: no evidence, no plan
    CHECK(f.fs.files.count("/data/adb/.config/flux/compat_provider/plans/bad.json") == 0);                // hostile name skipped

    Plan two;
    std::string err;
    plan_from_json(f.fs.files["/data/adb/.config/flux/compat_provider/plans/com.example.two.json"], two, err);
    CHECK((two.layers == std::vector<Layer>{Layer::Device, Layer::Gpu}));
    CHECK(two.scope.kind == ProcessScope::Kind::All);           // the profile said so explicitly
    Plan one;
    plan_from_json(f.fs.files[kPlan], one, err);
    CHECK((one.layers == std::vector<Layer>{Layer::Device}));
    CHECK(one.scope.kind == ProcessScope::Kind::Main);          // the default is the package's main process only
}

void test_arm_all_follows_profile_edits_and_withdrawn_consent() {
    Fixture f;
    f.provider_loaded();
    arm_all(*f.arming, docs(), false);
    CHECK(armed_list_from_text(f.fs.files[kArmed]).size() == 2);

    // The user switches one game back to the real device: it must be disarmed.
    auto in = docs();
    in.profiles_json = R"({"com.example.game":{"compatibility":{"mode":"real"}},
                           "com.example.two":{"compatibility":{"mode":"advanced","device_profile":"dev"}}})";
    arm_all(*f.arming, in, false);
    auto list = armed_list_from_text(f.fs.files[kArmed]);
    CHECK(list.size() == 1 && list[0].package == "com.example.two");
    Plan p;
    std::string err;
    plan_from_json(f.fs.files[kPlan], p, err);
    CHECK(!p.active);

    // tweaks disabled: everything comes off
    arm_all(*f.arming, docs(), true);
    CHECK(armed_list_from_text(f.fs.files[kArmed]).empty());

    // consent withdrawn: nothing stays armed
    arm_all(*f.arming, docs(), false);
    CHECK(!armed_list_from_text(f.fs.files[kArmed]).empty());
    f.arming->set_user_enabled(false);
    arm_all(*f.arming, docs(), false);
    CHECK(armed_list_from_text(f.fs.files[kArmed]).empty());
}

void test_arm_all_reports_a_broken_profile_without_arming_it() {
    Fixture f;
    auto in = docs();
    in.profiles_json = R"({"com.example.game":{"performance":{"memory":"turbo"}}})";
    auto rep = arm_all(*f.arming, in, false);
    CHECK(rep.armed.empty());
    CHECK(rep.failed.size() == 1 && rep.failed[0].first == "com.example.game");
    CHECK(f.fs.files.count(kPlan) == 0);
}

// ---- the daemon lifecycle with the real backend, provider simulated -----------------------------------------------

struct LifecycleRig {
    Fixture f;
    ZygiskBackend zb;
    std::unique_ptr<SessionRuntime> rt;
    std::vector<std::string> info, warn;
    LifecycleRig() : zb(*f.arming) {
        SessionDeps d;
        d.runtime.io = f.fs.io();
        d.runtime.native = &f.native;
        d.runtime.zygisk = &zb;
        d.runtime.library = &f.lib;
        d.journal_path = "/cfg/compat_journal";
        d.status_path = "/cfg/compat_status.json";
        d.inputs = [this](const SessionKey &k, ResolvedInputs &out, std::string &err) {
            return build_inputs(k.package, std::nullopt, Mode::Real, docs(), out, err);
        };
        d.info = [this](const std::string &m) { info.push_back(m); };
        d.warn = [this](const std::string &m) { warn.push_back(m); };
        rt = std::make_unique<SessionRuntime>(d);
    }
    bool logged(const std::string &n) const {
        for (auto &l : info) if (l.find(n) != std::string::npos) return true;
        for (auto &l : warn) if (l.find(n) != std::string::npos) return true;
        return false;
    }
};

void test_session_with_a_verified_process() {
    LifecycleRig r;
    r.f.provider_loaded();
    arm_all(*r.f.arming, docs(), false);                   // daemon boot
    std::string tx = r.f.arming->transaction_of(kPkg);
    r.f.report(kPid, tx, ProcState::Verified, {{Layer::Device, "verified", "Build fields read back"}});   // the game was launched

    CHECK(r.rt->begin({kPkg, static_cast<int>(kPid), 10123}));
    CHECK(r.rt->context() == ContextState::Active);
    CHECK(r.logged("provider=verified"));
    CHECK(r.logged("compatibility=ACTIVE backend=zygisk"));
    std::string status = r.f.fs.files["/cfg/compat_status.json"];
    CHECK(status.find("\"state\":\"verified\"") != std::string::npos);      // provider block
    CHECK(status.find("\"device\":\"verified\"") != std::string::npos);

    r.rt->end(EndReason::Exit);
    CHECK(!r.f.arming->transaction_of(kPkg).empty());      // still armed for the next launch
    CHECK(r.logged("restore=PASS"));
}

void test_session_when_the_game_was_already_running() {
    LifecycleRig r;
    r.f.provider_loaded();                                  // provider fine; the daemon just was not there yet
    CHECK(r.rt->begin({kPkg, static_cast<int>(kPid), 10123}));
    CHECK(r.rt->context() == ContextState::Failed);
    CHECK(r.logged("compatibility=FAILED"));
    CHECK(r.logged("performance_fallback=CONTINUE"));
    CHECK(r.logged("relaunch the game"));
    CHECK(!r.f.arming->transaction_of(kPkg).empty());      // armed now: the NEXT launch is covered
    r.rt->end(EndReason::Exit);
}

} // namespace

int main() {
    test_arm_all_arms_only_what_the_resolver_requires();
    test_arm_all_follows_profile_edits_and_withdrawn_consent();
    test_arm_all_reports_a_broken_profile_without_arming_it();
    test_session_with_a_verified_process();
    test_session_when_the_game_was_already_running();
    test_states_installed_opted_in_and_loaded();
    test_arming_writes_a_valid_plan_and_the_prefilter_list();
    test_rearming_is_stable_and_a_changed_plan_gets_a_new_transaction();
    test_disarm_and_no_layers();
    test_arming_refused_without_provider_or_opt_in();
    test_verified_process_makes_the_context_active_and_the_plan_outlives_the_session();
    test_installed_hooks_are_applied_not_verified();
    test_process_that_predates_the_plan_is_not_reported_active();
    test_provider_never_loaded_is_reported_as_such();
    test_stale_transaction_is_detected();
    test_provider_refusal_and_dead_process_are_failures();
    test_status_of_another_process_is_ignored();
    test_layer_failure_reported_by_the_provider();
    test_arming_failure_keeps_performance();
    test_no_provider_reports_unavailable_and_writes_nothing();
    test_real_mode_and_unknown_game_arm_nothing();
    test_supports_rejects_hostile_package_names();
    test_process_scope_reaches_the_plan();
    return flux_test::report("zygisk_backend_test");
}
