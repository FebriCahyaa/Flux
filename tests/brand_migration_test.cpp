// Brand & namespace migration (Phase 4.5): public labels change, compatibility identifiers do not,
// configuration migration is non-destructive, idempotent and deterministic, and no runtime
// mechanism is duplicated.
#include "flux_test.hpp"
#include "Brand.hpp"
#include "ConfigNamespace.hpp"
#include "Event.hpp"
#include "Flux.hpp"
#include "SynreiThermalAdapter.hpp"
#include "TelemetryStore.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace z = zairenkai::brand;
namespace o = flux::observatory;

namespace {

std::string source(const std::string &rel) {
    std::ifstream in(std::string(FLUX_SOURCE_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct MemFs {
    std::map<std::string, std::string> files;
    int writes = 0;
    z::ConfigFs fs() {
        z::ConfigFs f;
        f.exists = [this](const std::string &p) { return files.count(p) > 0; };
        f.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = files.find(p);
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
        f.create_new = [this](const std::string &p, const std::string &c) {
            ++writes;
            if (files.count(p)) return false; // create-only
            files[p] = c;
            return true;
        };
        return f;
    }
};

const z::ConfigRoots roots;
const std::string L = "/data/adb/.config/flux/", N = "/data/adb/.config/zairenkai/config/";

// 1, 9, 10. legacy fluxd and the public alias reach the same CLI; labels are Zairenkai.
void test_cli_names_and_labels() {
    CHECK_EQ(z::command_name("fluxd"), std::string("fluxd"));
    CHECK_EQ(z::command_name("/system/bin/fluxd"), std::string("fluxd"));
    CHECK_EQ(z::command_name("/data/adb/ksu/bin/zairenkai"), std::string("zairenkai"));
    CHECK_EQ(z::command_name("zairenkai"), std::string("zairenkai"));
    CHECK_EQ(z::command_name(""), std::string("fluxd"));
    CHECK_EQ(z::command_name("other"), std::string("fluxd"));
    CHECK_EQ(z::cli_banner(), std::string("Zairenkai CLI (Flux Tweaks runtime)"));
    CHECK_EQ(z::version_line("v1.4.1 (100)"), std::string("Zairenkai v1.4.1 (100) (Flux Tweaks runtime)"));
    CHECK_EQ(std::string(z::kThermal), std::string("Synrei Thermal Intelligence"));
    CHECK_EQ(std::string(z::kIntelligence), std::string("Zairenkai Intelligence"));
    // One dispatcher: the CLI keeps a single command table and handler loop.
    const auto cli = source("jni/FluxCLI.cpp");
    CHECK(cli.find("int flux_cli(") != std::string::npos);
    CHECK(cli.find("telemetry") != std::string::npos);
    CHECK(cli.find("zairenkai_cli") == std::string::npos);
    // The public name is a symlink to the one fluxd binary, on both mount paths.
    const auto install = source("module/customize.sh");
    CHECK(install.find("ln -sf fluxd \"$MODPATH/system/bin/zairenkai\"") != std::string::npos);
    CHECK(install.find("ln -sf \"$BIN_PATH/fluxd\" \"$dir/fluxd\"") != std::string::npos);
    CHECK(install.find("ln -sf \"$BIN_PATH/fluxd\" \"$dir/zairenkai\"") != std::string::npos);
    CHECK(source("module/uninstall.sh").find("for bin in fluxd zairenkai flux_profiler flux_utility") != std::string::npos);
}

// 3, 4, 5, 6, 7, 8, 12, 13. frozen identifiers.
void test_frozen_identifiers() {
    CHECK_EQ(std::string(CONFIG_DIR), std::string(z::kLegacyConfigDir));
    CHECK_EQ(std::string(MODPATH), std::string("/data/adb/modules/flux"));
    CHECK_EQ(std::string(LOG_TAG), std::string(z::kLogTag));
    CHECK_EQ("/" + std::string(flux::thermal::kSynreiStatePath), std::string(z::kHicoStatePath));
    CHECK_EQ(std::string(o::kTelemetryRoot), std::string(z::kConfigDir));
    CHECK_EQ(o::kSchemaVersion, 1);
    const auto registry = o::EventRegistry::builtin();
    const auto &types = registry.types();
    CHECK_EQ(types.size(), size_t(19));
    for (auto name : {"BOTTLENECK_ASSESSED", "BOTTLENECK_ANALYSIS_FAILED", "OBSERVATORY_STORAGE_FAILED", "TRANSACTION_BEGIN",
                      "TRANSACTION_RESTORE"})
        CHECK(types.count(name) == 1);
    CHECK(source("jni/observatory/InstallationEpoch.hpp").find("/data/adb/.config/zairenkai/installation.json") !=
          std::string::npos);
    const auto prop = source("module/module.prop");
    CHECK(prop.find("id=flux\n") != std::string::npos);
    CHECK(prop.find("updateJson=https://raw.githubusercontent.com/FebriCahyaa/Flux/main/update.json") != std::string::npos);
    CHECK(prop.find("name=Zairenkai\n") != std::string::npos);
    CHECK(source(".github/scripts/compile_zip.sh").find("s#/main/update.json#/main/$update_json#") != std::string::npos);
    const auto service = source("module/service.sh");
    CHECK(service.find(std::string(z::kIntelligencePackage)) != std::string::npos);
    CHECK(service.find("fluxd daemon") != std::string::npos);
    CHECK(source("scripts/flux_profiler.sh").find(std::string(z::kHicoConfigDir)) != std::string::npos);
}

// 3, 11, 21, 22, 23, 24. configuration lookup and migration.
void test_config_migration() {
    MemFs m;
    m.files[L + "config.json"] = "legacy";
    m.files[L + "gamelist.json"] = "games";
    m.files[N + "gamelist.json"] = "user-new";
    auto fs = m.fs();
    CHECK_EQ(z::resolve(fs, roots, "config.json"), L + "config.json"); // legacy stays readable
    CHECK_EQ(z::resolve(fs, roots, "gamelist.json"), N + "gamelist.json");
    CHECK_EQ(z::resolve(fs, roots, "../x"), L + "../x"); // never looked up in the new tree

    const std::vector<std::string> names{"config.json", "gamelist.json", "absent.json", "../escape", "a/b", ""};
    auto p1 = z::plan(fs, roots, names);
    CHECK(p1[0].kind == z::StepKind::Copy);
    CHECK(p1[1].kind == z::StepKind::KeepExisting);
    CHECK(p1[2].kind == z::StepKind::NoLegacy);
    CHECK(p1[3].kind == z::StepKind::Rejected && p1[4].kind == z::StepKind::Rejected && p1[5].kind == z::StepKind::Rejected);
    auto p1b = z::plan(fs, roots, names); // 21. deterministic
    for (size_t i = 0; i < p1.size(); ++i) CHECK(p1[i].kind == p1b[i].kind && p1[i].to == p1b[i].to);
    const auto before = m.files;
    auto r1 = z::apply(fs, p1);
    CHECK_EQ(r1.copied, 1);
    CHECK_EQ(r1.kept, 1);
    CHECK_EQ(r1.rejected, 3);
    CHECK_EQ(m.files[N + "config.json"], std::string("legacy"));
    CHECK_EQ(m.files[N + "gamelist.json"], std::string("user-new")); // 23. never overwritten
    for (auto &[k, v] : before) CHECK_EQ(m.files.at(k), v);         // 11. nothing lost or changed
    CHECK_EQ(m.files.at(L + "config.json"), std::string("legacy"));   // legacy kept

    const auto after1 = m.files; // 22, 24. rerun is a no-op
    const int writes = m.writes;
    auto r2 = z::apply(fs, z::plan(fs, roots, names));
    CHECK_EQ(r2.copied, 0);
    CHECK_EQ(r2.kept, 2);
    CHECK(m.files == after1);
    CHECK_EQ(m.writes, writes);

    // A target appearing between plan and apply is not overwritten.
    MemFs race;
    race.files[L + "a.json"] = "old";
    auto rfs = race.fs();
    auto plan = z::plan(rfs, roots, {"a.json"});
    race.files[N + "a.json"] = "user";
    auto rr = z::apply(rfs, plan);
    CHECK_EQ(rr.copied, 0);
    CHECK_EQ(race.files[N + "a.json"], std::string("user"));
}

// 15-20. no duplicated runtime, daemon, thermal backend or transaction framework; policy untouched.
void test_no_duplication() {
    for (auto f : {"jni/brand/Brand.hpp", "jni/brand/Brand.cpp", "jni/brand/ConfigNamespace.hpp", "jni/brand/ConfigNamespace.cpp"}) {
        const auto s = source(f);
        CHECK(!s.empty());
        for (auto bad : {"int main", "daemon(", "std::thread", "pthread", "Transaction", "journal::", "SynreiThermalAdapter",
                         "/dev/hico/state\"", "remove(", "unlink", "rename(", "ofstream", "system(", "popen", "DecisionEngine",
                         "PolicyExecutor"})
            CHECK(f == std::string("jni/brand/Brand.hpp") && std::string(bad) == "/dev/hico/state\""
                      ? true
                      : s.find(bad) == std::string::npos);
    }
    const auto mk = source("jni/Android.mk");
    size_t execs = 0;
    for (size_t pos = 0; (pos = mk.find("BUILD_EXECUTABLE", pos)) != std::string::npos; ++pos) ++execs;
    CHECK_EQ(execs, size_t(1)); // still exactly one daemon binary: fluxd
    CHECK(mk.find("LOCAL_MODULE := fluxd") != std::string::npos);
    for (auto f : {"jni/policy/DecisionEngine.cpp", "jni/policy/PolicyExecutor.cpp", "jni/policy/LivePolicyController.cpp",
                   "jni/runtime/Transaction.cpp"}) {
        const auto s = source(f);
        CHECK(!s.empty());
        CHECK(s.find("brand") == std::string::npos && s.find("Zairenkai") == std::string::npos);
    }
}

} // namespace

int main() {
    test_cli_names_and_labels();
    test_frozen_identifiers();
    test_config_migration();
    test_no_duplication();
    return flux_test::report("brand_migration_test");
}
