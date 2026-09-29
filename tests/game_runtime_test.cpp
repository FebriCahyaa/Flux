// Host tests for the Game Runtime performance lifecycle (jni/perf/GamePerformanceRuntime.*).
// Maps stand in for kernel nodes and config files.

#include "flux_test.hpp"

#include "GamePerformanceRuntime.hpp"

#include <map>
#include <set>

using namespace flux::perf;

namespace {

const std::string kQ = "/sys/block/sda/queue";
const std::string kSwap = "/proc/sys/vm/swappiness";
const std::string kBoost = "/sys/module/cpu_boost/parameters/input_boost_ms";
const std::string kUclamp = "/dev/cpuctl/top-app/cpu.uclamp.min";

struct World {
    std::map<std::string, std::string> nodes;
    std::map<std::string, std::string> files;
    std::set<std::string> fail_write;
    std::map<std::string, bool> gamelist;
    std::vector<std::string> logs;

    World() {
        for (auto n : {"swappiness", "vfs_cache_pressure", "page-cluster", "dirty_expire_centisecs",
                       "dirty_writeback_centisecs", "watermark_boost_factor"})
            nodes[std::string("/proc/sys/vm/") + n] = "100";
        nodes[kBoost] = "0";
        nodes[kQ + "/read_ahead_kb"] = "128";
        nodes[kQ + "/rq_affinity"] = "1";
        nodes[kUclamp] = "0";
    }

    RuntimeDeps deps() {
        RuntimeDeps d;
        d.node_io.exists = [this](const std::string &p) { return nodes.count(p) > 0; };
        d.node_io.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = nodes.find(p);
            if (it == nodes.end()) return std::nullopt;
            return it->second;
        };
        d.node_io.write = [this](const std::string &p, const std::string &v) {
            if (fail_write.count(p)) return false;
            nodes[p] = v;
            return true;
        };
        d.files.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = files.find(p);
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
        d.files.write_atomic = [this](const std::string &p, const std::string &t) {
            files[p] = t;
            return true;
        };
        d.files.remove = [this](const std::string &p) {
            files.erase(p);
            return true;
        };
        d.paths = {"/cfg/game_profiles.json", "/cfg/compat_library.json", "/cfg/perf_journal",
                   "/cfg/launch_journal", "/cfg/compat_journal"};
        d.caps.node_available = [this](const std::string &p) { return nodes.count(p) > 0; };
        d.caps.block_queues = {kQ};
        d.caps.panel_refresh_hz = {60, 120};
        d.gamelist_lite = [this](const std::string &pkg) -> std::optional<bool> {
            auto it = gamelist.find(pkg);
            if (it == gamelist.end()) return std::nullopt;
            return it->second;
        };
        d.log = [this](const std::string &m) { logs.push_back(m); };
        return d;
    }
};

const char *kProfiles = R"({"version":1,
  "presets": {"gaming": {"memory": "gaming", "touch": "competitive", "refresh": "hz120"}},
  "games": {"com.a": {"extends": "gaming"},
            "com.b": {"storage": "gaming"},
            "com.boost": {"launch_boost": true, "touch": "balanced"}}})";

bool has_log(const World &w, const std::string &needle) {
    for (const auto &l : w.logs)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

void test_game_start_applies_profile() {
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.a", 100, 0));
    CHECK(rt.state() == RuntimeState::Active);
    CHECK_EQ(w.nodes[kSwap], std::string("60"));
    CHECK_EQ(w.nodes[kBoost], std::string("160"));
    CHECK_EQ(rt.refresh_target_hz(), 120);
    CHECK_EQ(rt.profile().sources.at("memory").describe(), std::string("preset gaming"));
    CHECK(w.files.count("/cfg/perf_journal") == 1); // write-ahead journal on disk
    CHECK(!rt.on_game_start("com.a", 100, 1000));   // same game, same pid: no-op

    // A game without a profile: nothing applied, nothing changed.
    World w2;
    GamePerformanceRuntime rt2(w2.deps());
    CHECK(rt2.on_game_start("com.none", 5, 0));
    CHECK(rt2.state() == RuntimeState::Idle);
    CHECK_EQ(w2.nodes[kSwap], std::string("100"));
}

void test_game_exit_restores() {
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.a", 100, 0));
    w.nodes[kSwap] = "30"; // profile script overwrote it
    CHECK(rt.after_profile_script());
    CHECK_EQ(w.nodes[kSwap], std::string("60"));
    CHECK(rt.on_game_end(EndReason::Exit));
    CHECK(rt.state() == RuntimeState::Idle);
    CHECK_EQ(w.nodes[kSwap], std::string("100"));
    CHECK_EQ(w.nodes[kBoost], std::string("0"));
    CHECK_EQ(rt.refresh_target_hz(), 0);
    CHECK(w.files.count("/cfg/perf_journal") == 0); // clean restore removes the journal
    CHECK(rt.on_game_end(EndReason::Exit));         // idempotent
}

void test_process_death_rollback() {
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.boost", 7, 0));
    CHECK(rt.launch_boost_active());
    CHECK_EQ(w.nodes[kUclamp], std::string("50"));
    CHECK(rt.on_game_end(EndReason::ProcessDeath));
    CHECK(!rt.launch_boost_active());
    CHECK_EQ(w.nodes[kUclamp], std::string("0"));
    CHECK_EQ(w.nodes[kQ + "/read_ahead_kb"], std::string("128"));
    CHECK_EQ(w.nodes[kBoost], std::string("0"));
    CHECK(has_log(w, "process_death"));

    // Launch boost ends on its own deadline while the game keeps running.
    World w2;
    w2.files["/cfg/game_profiles.json"] = kProfiles;
    GamePerformanceRuntime rt2(w2.deps());
    CHECK(rt2.on_game_start("com.boost", 7, 0));
    rt2.tick(20000);
    CHECK(!rt2.launch_boost_active());
    CHECK_EQ(w2.nodes[kUclamp], std::string("0"));
    CHECK(rt2.state() == RuntimeState::Active);     // per-game touch still applied
    CHECK_EQ(w2.nodes[kBoost], std::string("40"));
}

void test_daemon_restart_recovery() {
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    {
        GamePerformanceRuntime rt(w.deps());
        CHECK(rt.on_game_start("com.a", 100, 0));
        // daemon killed: no on_game_end
    }
    CHECK_EQ(w.nodes[kSwap], std::string("60"));
    // A legacy journal from the old branch is recovered too.
    w.nodes[kQ + "/rq_affinity"] = "2";
    w.files["/cfg/compat_journal"] = flux::runtime::journal::encode_entry(kQ + "/rq_affinity", "1") + "\n";

    GamePerformanceRuntime fresh(w.deps());
    auto results = fresh.recover();
    CHECK_EQ(w.nodes[kSwap], std::string("100"));
    CHECK_EQ(w.nodes[kBoost], std::string("0"));
    CHECK_EQ(w.nodes[kQ + "/rq_affinity"], std::string("1"));
    CHECK(w.files.count("/cfg/perf_journal") == 0);
    CHECK(w.files.count("/cfg/compat_journal") == 0);
    bool all_clean = true;
    for (const auto &r : results) all_clean = all_clean && r.report.clean();
    CHECK(all_clean);

    // A journal that cannot be fully restored is kept for the next boot.
    World w3;
    w3.nodes[kSwap] = "60";
    w3.files["/cfg/perf_journal"] = flux::runtime::journal::encode_entry(kSwap, "100") + "\nbroken-line\n";
    GamePerformanceRuntime r3(w3.deps());
    r3.recover();
    CHECK_EQ(w3.nodes[kSwap], std::string("100"));
    CHECK(w3.files.count("/cfg/perf_journal") == 1);
}

void test_transaction_failure_fallback() {
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    w.fail_write.insert(kBoost); // touch write fails after memory writes
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.a", 100, 0));
    CHECK(rt.state() == RuntimeState::Failed);
    CHECK_EQ(w.nodes[kSwap], std::string("100")); // rolled back
    CHECK_EQ(rt.refresh_target_hz(), 0);          // nothing requested on failure
    CHECK(!rt.last_error().empty());
    CHECK(rt.on_game_end(EndReason::Exit));       // nothing left to restore
    CHECK(rt.state() == RuntimeState::Idle);
}

void test_profile_resolve_failure() {
    World w;
    w.files["/cfg/game_profiles.json"] = R"({"version":1,"games":{"com.a":{"extends":"ghost","memory":"gaming"}}})";
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.a", 1, 0));
    CHECK(rt.state() == RuntimeState::ResolveFailed);
    CHECK_EQ(w.nodes[kSwap], std::string("100")); // fail closed: nothing applied
    CHECK(rt.last_error().find("unknown profile 'ghost'") != std::string::npos);

    World w2;
    w2.files["/cfg/game_profiles.json"] = "{not json";
    GamePerformanceRuntime rt2(w2.deps());
    CHECK(rt2.on_game_start("com.a", 1, 0));
    CHECK(rt2.state() == RuntimeState::ResolveFailed);
    CHECK_EQ(w2.nodes[kSwap], std::string("100"));
}

void test_multiple_game_lifecycle_safety() {
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.a", 100, 0));
    CHECK_EQ(w.nodes[kSwap], std::string("60"));
    CHECK(rt.on_game_start("com.b", 200, 1000)); // switch: A restored first
    CHECK_EQ(rt.package(), std::string("com.b"));
    CHECK_EQ(w.nodes[kSwap], std::string("100"));
    CHECK_EQ(w.nodes[kBoost], std::string("0"));
    CHECK_EQ(w.nodes[kQ + "/rq_affinity"], std::string("2"));
    CHECK(rt.on_game_start("com.b", 201, 2000)); // same game, restarted process
    CHECK_EQ(rt.pid(), 201);
    CHECK_EQ(w.nodes[kQ + "/rq_affinity"], std::string("2"));
    CHECK(rt.on_game_end(EndReason::DaemonStop));
    CHECK_EQ(w.nodes[kQ + "/rq_affinity"], std::string("1"));
    CHECK_EQ(w.nodes[kQ + "/read_ahead_kb"], std::string("128"));
}

void test_compatibility_fields_ignored() {
    World w;
    w.files["/cfg/game_profiles.json"] = R"({"com.old": {"performance": {"memory": "balanced"},
        "compatibility": {"mode": "auto", "gpu_profile": "flagship"}}})";
    w.files["/cfg/compat_library.json"] = R"({"identities": {"flagship": {"layer": "gpu"}}})";
    GamePerformanceRuntime rt(w.deps());
    CHECK(rt.on_game_start("com.old", 3, 0));
    CHECK(rt.state() == RuntimeState::Active);
    CHECK_EQ(w.nodes["/proc/sys/vm/page-cluster"], std::string("0"));
    bool warned = false;
    for (const auto &m : rt.warnings()) warned = warned || m.find("compatibility ignored") != std::string::npos;
    CHECK(warned);

    // gamelist lite_mode is used when the game has no profile entry.
    World g;
    g.gamelist["com.lite"] = true;
    GamePerformanceRuntime rg(g.deps());
    rg.on_game_start("com.lite", 4, 0);
    CHECK_EQ(rg.profile().profile, std::string("performance_lite"));
}

} // namespace

int main() {
    test_game_start_applies_profile();
    test_game_exit_restores();
    test_process_death_rollback();
    test_daemon_restart_recovery();
    test_transaction_failure_fallback();
    test_profile_resolve_failure();
    test_multiple_game_lifecycle_safety();
    test_compatibility_fields_ignored();
    return flux_test::report("game_runtime_test");
}
