// Host tests for the Performance Planner and LaunchBoost (jni/perf/*).
// A map stands in for /sys, /proc and /dev; nothing touches the real filesystem.

#include "flux_test.hpp"

#include "PerformancePlanner.hpp"

#include <algorithm>
#include <map>
#include <set>

using namespace flux::perf;
using flux::runtime::Io;
using flux::runtime::Transaction;
using flux::runtime::TxState;

namespace {

struct FakeFs {
    std::map<std::string, std::string> nodes;
    std::set<std::string> fail_write;
    int writes = 0;

    Io io() {
        Io io;
        io.exists = [this](const std::string &p) { return nodes.count(p) > 0; };
        io.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = nodes.find(p);
            if (it == nodes.end()) return std::nullopt;
            return it->second;
        };
        io.write = [this](const std::string &p, const std::string &v) {
            ++writes;
            if (fail_write.count(p)) return false;
            nodes[p] = v;
            return true;
        };
        return io;
    }
    PerfCapabilities caps(std::vector<std::string> queues = {}, std::vector<int> hz = {}) {
        PerfCapabilities c;
        c.node_available = [this](const std::string &p) { return nodes.count(p) > 0; };
        c.block_queues = std::move(queues);
        c.panel_refresh_hz = std::move(hz);
        return c;
    }
};

const std::string kQ = "/sys/block/sda/queue";

FakeFs full_device() {
    FakeFs fs;
    for (auto n : {"swappiness", "vfs_cache_pressure", "page-cluster", "dirty_expire_centisecs",
                   "dirty_writeback_centisecs", "watermark_boost_factor"})
        fs.nodes[std::string("/proc/sys/vm/") + n] = "100";
    fs.nodes["/sys/module/cpu_boost/parameters/input_boost_ms"] = "0";
    fs.nodes[kQ + "/read_ahead_kb"] = "128";
    fs.nodes[kQ + "/rq_affinity"] = "1";
    fs.nodes["/dev/cpuctl/top-app/cpu.uclamp.min"] = "0";
    return fs;
}

const CategoryReport *report_of(const PerformancePlanResult &r, const std::string &cat) {
    for (const auto &c : r.report)
        if (c.category == cat) return &c;
    return nullptr;
}

bool has_path(const flux::runtime::RuntimePlan &p, const std::string &path) {
    return std::any_of(p.operations.begin(), p.operations.end(),
                       [&](const auto &op) { return op->describe().rfind(path + " = ", 0) == 0; });
}

void test_profile_to_plan_conversion() {
    FakeFs fs = full_device();
    PerformancePlanner planner(fs.io(), fs.caps({kQ}, {60, 90, 120}));
    PerfProfile p;
    p.memory = "gaming";
    p.touch = "responsive";
    p.storage = "gaming";
    p.refresh = "hz90";
    auto r = planner.plan(p, {"com.example.game"});

    CHECK(r.errors.empty());
    CHECK_EQ(r.plan.domain, std::string("performance"));
    CHECK_EQ(r.plan.subject, std::string("com.example.game"));
    CHECK_EQ(r.plan.operations.size(), size_t{5 + 1 + 2});
    CHECK(has_path(r.plan, "/proc/sys/vm/swappiness"));
    CHECK(has_path(r.plan, "/sys/module/cpu_boost/parameters/input_boost_ms"));
    CHECK(has_path(r.plan, kQ + "/rq_affinity"));
    CHECK_EQ(r.refresh_target_hz, 90);
    CHECK(report_of(r, "memory")->support == Support::Supported);
    CHECK_EQ(fs.writes, 0); // planning never writes

    // "default" everywhere: empty plan, every category Auto.
    auto none = planner.plan(PerfProfile{}, {"com.example.game"});
    CHECK(none.plan.operations.empty());
    CHECK(report_of(none, "memory")->support == Support::Auto);
    CHECK_EQ(none.refresh_target_hz, 0);

    // Invalid selections are rejected, never guessed.
    PerfProfile bad;
    bad.memory = "turbo";
    bad.refresh = "hz999x";
    auto rej = planner.plan(bad, {"g"});
    CHECK_EQ(rej.errors.size(), size_t{2});
    CHECK(rej.plan.operations.empty());
}

void test_unsupported_node_handling() {
    FakeFs fs; // nothing present
    fs.nodes["/proc/sys/vm/page-cluster"] = "3";
    PerformancePlanner planner(fs.io(), fs.caps({}, {60}));
    PerfProfile p;
    p.memory = "gaming";
    p.touch = "competitive";
    p.refresh = "hz120"; // panel only has 60
    auto r = planner.plan(p, {"g"});
    CHECK_EQ(r.plan.operations.size(), size_t{1}); // only page-cluster exists
    CHECK_EQ(report_of(r, "memory")->available, 1);
    CHECK(report_of(r, "touch")->support == Support::Unsupported);
    CHECK_EQ(r.refresh_target_hz, 0); // a rate the panel does not offer is never requested
    CHECK(report_of(r, "refresh")->support == Support::Unsupported);

    // Mitigation blocks a category instead of being bypassed.
    FakeFs full = full_device();
    PerformancePlanner mitigated(full.io(), full.caps({kQ}),
                                 [](const std::string &cat) { return cat != "storage"; });
    PerfProfile s;
    s.storage = "gaming";
    auto m = mitigated.plan(s, {"g"});
    CHECK(m.plan.operations.empty());
    CHECK(report_of(m, "storage")->blocked_by_mitigation);

    // Interface check: a queue path outside /sys/block/*/queue is never planned.
    FakeFs weird = full_device();
    weird.nodes["/data/evil/read_ahead_kb"] = "1";
    PerformancePlanner iface(weird.io(), weird.caps({"/data/evil"}));
    auto w = iface.plan(s, {"g"});
    CHECK(!has_path(w.plan, "/data/evil/read_ahead_kb"));
    CHECK(report_of(w, "storage")->rejected_interface > 0);
    CHECK(PerformancePlanner::interface_allowed("/sys/block/mmcblk0/queue/read_ahead_kb"));
    CHECK(!PerformancePlanner::interface_allowed("/sys/block/../../data/x/queue/read_ahead_kb"));
}

void test_plan_executes_through_transaction() {
    FakeFs fs = full_device();
    PerformancePlanner planner(fs.io(), fs.caps({kQ}));
    PerfProfile p;
    p.memory = "balanced";
    auto r = planner.plan(p, {"g"});
    Transaction tx("tx-perf-1", std::move(r.plan));
    CHECK(tx.start());
    CHECK_EQ(fs.nodes["/proc/sys/vm/vfs_cache_pressure"], std::string("100")); // same value is fine
    CHECK_EQ(fs.nodes["/proc/sys/vm/page-cluster"], std::string("0"));
    CHECK(tx.finish());
    CHECK_EQ(fs.nodes["/proc/sys/vm/page-cluster"], std::string("100"));
}

void test_transaction_failure_rollback() {
    FakeFs fs = full_device();
    fs.fail_write.insert("/proc/sys/vm/watermark_boost_factor"); // last memory write fails
    PerformancePlanner planner(fs.io(), fs.caps({kQ}));
    PerfProfile p;
    p.memory = "gaming";
    Transaction tx("tx-perf-2", planner.plan(p, {"g"}).plan);
    CHECK(!tx.start());
    CHECK(tx.state() == TxState::Failed);
    CHECK_EQ(fs.nodes["/proc/sys/vm/swappiness"], std::string("100")); // rolled back
    CHECK_EQ(fs.nodes["/proc/sys/vm/page-cluster"], std::string("100"));
}

void test_launch_boost_timeout() {
    FakeFs fs = full_device();
    PerformancePlanner planner(fs.io(), fs.caps({kQ}));
    LaunchBoost boost(planner, 8000);
    CHECK(boost.begin({"g"}, 1000, "tx-lb-1"));
    CHECK(boost.phase() == LaunchBoost::Phase::Boosting);
    CHECK_EQ(fs.nodes[kQ + "/read_ahead_kb"], std::string("1024"));
    CHECK_EQ(fs.nodes["/dev/cpuctl/top-app/cpu.uclamp.min"], std::string("50"));
    CHECK(!boost.begin({"g"}, 2000, "tx-lb-2")); // one boost at a time
    CHECK(boost.tick(8999));
    CHECK(!boost.tick(9000)); // deadline reached -> restored
    CHECK(boost.phase() == LaunchBoost::Phase::Done);
    CHECK_EQ(boost.last_cancel(), std::string("timeout"));
    CHECK(boost.restored_clean());
    CHECK_EQ(fs.nodes[kQ + "/read_ahead_kb"], std::string("128"));
    CHECK_EQ(fs.nodes["/dev/cpuctl/top-app/cpu.uclamp.min"], std::string("0"));

    // Duration is bounded both ways.
    CHECK_EQ(LaunchBoost(planner, 600000).max_ms(), LaunchBoost::kMaxMs);
    CHECK_EQ(LaunchBoost(planner, 10).max_ms(), LaunchBoost::kMinMs);
}

void test_launch_boost_cancellation() {
    FakeFs fs = full_device();
    PerformancePlanner planner(fs.io(), fs.caps({kQ}));
    LaunchBoost boost(planner);
    std::string journal;
    CHECK(boost.begin({"g"}, 0, "tx-lb-3", [&](const std::string &j) { journal = j; return true; }));
    CHECK(!flux::runtime::journal::parse(journal).entries.empty()); // snapshot-protected
    CHECK(boost.cancel(LaunchBoost::Cancel::ProcessExit));
    CHECK_EQ(boost.last_cancel(), std::string("process_exit"));
    CHECK_EQ(fs.nodes[kQ + "/read_ahead_kb"], std::string("128"));
    CHECK(flux::runtime::journal::parse(journal).entries.empty());
    CHECK(boost.cancel(LaunchBoost::Cancel::Watchdog)); // idempotent

    // Nothing available: no boost, nothing to undo.
    FakeFs empty;
    PerformancePlanner none(empty.io(), empty.caps());
    LaunchBoost nb(none);
    CHECK(!nb.begin({"g"}, 0, "tx-lb-4"));
    CHECK(nb.phase() == LaunchBoost::Phase::Done);

    // A failing write rolls the boost back and reports it.
    FakeFs broken = full_device();
    broken.fail_write.insert("/dev/cpuctl/top-app/cpu.uclamp.min");
    PerformancePlanner bp(broken.io(), broken.caps({kQ}));
    LaunchBoost fb(bp);
    CHECK(!fb.begin({"g"}, 0, "tx-lb-5"));
    CHECK_EQ(broken.nodes[kQ + "/read_ahead_kb"], std::string("128"));
}

} // namespace

int main() {
    test_profile_to_plan_conversion();
    test_unsupported_node_handling();
    test_plan_executes_through_transaction();
    test_transaction_failure_rollback();
    test_launch_boost_timeout();
    test_launch_boost_cancellation();
    return flux_test::report("performance_planner_test");
}
