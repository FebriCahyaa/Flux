// Host tests for the daemon-facing runtime host, the refresh bridge and the real-file adapters
// (jni/perf/RuntimeHost.*). Adapter tests run against a private temp directory; nothing outside
// it is read or written.

#include "flux_test.hpp"

#include "RuntimeHost.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace flux::perf;

namespace {

const std::string kSwap = "/proc/sys/vm/swappiness";
const std::string kBoost = "/sys/module/cpu_boost/parameters/input_boost_ms";

struct Fake {
    std::map<std::string, std::string> nodes{{kSwap, "100"}, {kBoost, "0"}};
    std::map<std::string, std::string> files;
    std::vector<int> panel{60, 90, 120};

    RuntimeDeps deps() {
        RuntimeDeps d;
        d.node_io.exists = [this](const std::string &p) { return nodes.count(p) > 0; };
        d.node_io.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = nodes.find(p);
            if (it == nodes.end()) return std::nullopt;
            return it->second;
        };
        d.node_io.write = [this](const std::string &p, const std::string &v) {
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
        d.paths = {"/cfg/game_profiles.json", "", "/cfg/perf_journal", "/cfg/launch_journal", ""};
        d.caps.node_available = [this](const std::string &p) { return nodes.count(p) > 0; };
        d.caps.panel_refresh_hz = panel;
        return d;
    }
};

const char *kProfiles = R"({"version":1,"games":{
  "com.a": {"memory": "gaming", "touch": "balanced", "refresh": "hz90"},
  "com.b": {"refresh": "hz144"}}})";

// -- daemon lifecycle ----------------------------------------------------------------------------

void test_daemon_lifecycle() {
    Fake f;
    f.files["/cfg/game_profiles.json"] = kProfiles;
    // Left behind by a killed fluxd: swappiness was raised to 60 and never restored.
    f.nodes[kSwap] = "60";
    f.files["/cfg/perf_journal"] = "flux-journal 1\ttx-old\tperformance\tcom.a\n" +
                                   flux::runtime::journal::encode_entry(kSwap, "100") + "\n";

    RuntimeHost host(f.deps());                     // fluxd start
    auto rec = host.on_daemon_start();              // recover journal
    CHECK_EQ(rec.size(), size_t{1});
    CHECK(rec[0].report.clean());
    CHECK_EQ(f.nodes[kSwap], std::string("100"));
    CHECK_EQ(f.files.count("/cfg/perf_journal"), size_t{0});

    host.on_game_active("com.a", 42, 1000);         // game start event
    CHECK(host.runtime().state() == RuntimeState::Active); // performance runtime activated
    CHECK_EQ(f.nodes[kSwap], std::string("60"));
    CHECK_EQ(f.nodes[kBoost], std::string("40"));
    host.on_game_active("com.a", 42, 2000);         // repeated profile pass: no re-activation
    f.nodes[kBoost] = "0";                          // profile script rewrote the node
    host.on_profile_applied();
    CHECK_EQ(f.nodes[kBoost], std::string("40"));

    host.on_game_end(EndReason::Exit);              // game exit -> restore called
    CHECK(host.runtime().state() == RuntimeState::Idle);
    CHECK_EQ(f.nodes[kSwap], std::string("100"));
    CHECK_EQ(f.nodes[kBoost], std::string("0"));
    CHECK_EQ(f.files.count("/cfg/perf_journal"), size_t{0});

    // disable_tweaks ends the context and ignores game events.
    host.on_game_active("com.a", 43, 3000);
    host.set_enabled(false);
    CHECK_EQ(f.nodes[kSwap], std::string("100"));
    host.on_game_active("com.a", 44, 4000);
    CHECK(host.runtime().state() == RuntimeState::Idle);
    CHECK_EQ(host.refresh_request_hz(), 0);
}

// -- refresh bridge ------------------------------------------------------------------------------

void test_refresh_bridge() {
    Fake f;
    f.files["/cfg/game_profiles.json"] = kProfiles;
    RuntimeHost host(f.deps());
    host.on_daemon_start();

    host.on_game_active("com.a", 1, 0);
    CHECK_EQ(host.refresh_request_hz(), 90);        // supported refresh applied
    host.on_game_end(EndReason::Exit);
    CHECK_EQ(host.refresh_request_hz(), 0);

    host.on_game_active("com.b", 2, 0);             // 144 Hz on a 60/90/120 panel
    CHECK_EQ(host.refresh_request_hz(), 0);         // unsupported refresh ignored
    host.on_game_end(EndReason::Exit);

    CHECK((parse_panel_rates("DisplayModeRecord{fps=60.0}\nDisplayModeRecord{fps=90.000015}\n"
                             "DisplayModeRecord{fps=119.99} x fps=60.0") == std::vector<int>{120, 90, 60}));
    CHECK(parse_panel_rates("no modes here").empty());
}

// -- real-file adapters --------------------------------------------------------------------------

std::string make_root() {
    char tmpl[] = "/tmp/flux_adapter_XXXXXX";
    const char *d = mkdtemp(tmpl);
    return d ? d : "";
}

void put(const std::string &path, const std::string &text) {
    std::string dir = path.substr(0, path.rfind('/'));
    std::string cmd = "mkdir -p '" + dir + "'";
    CHECK_EQ(std::system(cmd.c_str()), 0);
    std::ofstream(path) << text;
}

std::string get(const std::string &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void test_adapters() {
    const std::string root = make_root();
    CHECK(!root.empty());
    put(root + kSwap, "100\n");
    put(root + "/sys/block/sda/queue/read_ahead_kb", "128\n");
    put(root + "/sys/block/sda/queue/rq_affinity", "1\n");
    put(root + "/sys/block/loop0/queue/read_ahead_kb", "128\n"); // virtual: excluded
    put(root + "/sys/block/zram0/queue/read_ahead_kb", "128\n"); // virtual: excluded

    flux::runtime::Io io = make_node_io(root);
    PerfCapabilities caps = probe_capabilities(root, [] { return std::vector<int>{60, 120}; });
    CHECK(caps.node_available(kSwap));
    CHECK(!caps.node_available(kBoost));                          // missing on this "device"
    CHECK((caps.block_queues == std::vector<std::string>{"/sys/block/sda/queue"}));
    CHECK((caps.panel_refresh_hz == std::vector<int>{60, 120}));

    // Missing node skipped: touch asked, node absent -> not planned, memory still applied.
    PerformancePlanner planner(io, caps);
    PerfProfile p;
    p.memory = "gaming";
    p.touch = "competitive";
    p.storage = "gaming";
    auto r = planner.plan(p, {"g"});
    for (const auto &c : r.report)
        if (c.category == "touch") CHECK(c.support == Support::Unsupported);
    flux::runtime::Transaction tx("tx-a", std::move(r.plan));
    CHECK(tx.start());
    CHECK_EQ(get(root + kSwap), std::string("60"));
    CHECK_EQ(get(root + "/sys/block/sda/queue/rq_affinity"), std::string("2"));
    CHECK(tx.finish());
    CHECK_EQ(get(root + kSwap), std::string("100"));
    CHECK_EQ(get(root + "/sys/block/sda/queue/rq_affinity"), std::string("1"));

    // Rollback works on real files: the last write fails. The node is readable but every write
    // returns ENOSPC (a link to /dev/full), like a kernel node that rejects the value.
    put(root + "/proc/sys/vm/page-cluster", "3\n");
    CHECK_EQ(symlink("/dev/full", (root + "/proc/sys/vm/watermark_boost_factor").c_str()), 0);
    PerformancePlanner planner2(io, probe_capabilities(root, [] { return std::vector<int>{}; }));
    PerfProfile m;
    m.memory = "gaming";
    flux::runtime::Transaction tx2("tx-b", planner2.plan(m, {"g"}).plan);
    CHECK(!tx2.start());
    CHECK_EQ(get(root + kSwap), std::string("100"));
    CHECK_EQ(get(root + "/proc/sys/vm/page-cluster"), std::string("3"));

    // FileStore: atomic replace, read back, remove, refuses to follow a symlink.
    FileStore fs = make_file_store();
    const std::string j = root + "/cfg/perf_journal";
    CHECK_EQ(std::system(("mkdir -p '" + root + "/cfg'").c_str()), 0);
    CHECK(fs.write_atomic(j, "a\n"));
    CHECK(fs.write_atomic(j, "b\n"));
    CHECK_EQ(fs.read(j).value_or(""), std::string("b\n"));
    CHECK(fs.remove(j));
    CHECK(!fs.read(j).has_value());
    const std::string target = root + "/outside";
    put(target, "keep");
    CHECK_EQ(symlink(target.c_str(), j.c_str()), 0);
    CHECK(!fs.write_atomic(j, "evil"));
    CHECK_EQ(get(target), std::string("keep"));

    CHECK_EQ(std::system(("rm -rf '" + root + "'").c_str()), 0);
}

} // namespace

int main() {
    test_daemon_lifecycle();
    test_refresh_bridge();
    test_adapters();
    return flux_test::report("runtime_host_test");
}
