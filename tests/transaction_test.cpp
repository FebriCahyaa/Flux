// Host tests for the Runtime Transaction Engine (jni/runtime/Transaction.*).
// A map stands in for /sys and /proc; nothing touches the real filesystem.

#include "flux_test.hpp"

#include "Transaction.hpp"

#include <map>
#include <set>

using namespace flux::runtime;

namespace {

struct FakeFs {
    std::map<std::string, std::string> nodes;
    std::set<std::string> fail_write;   ///< writes to these paths fail
    std::set<std::string> ignore_write; ///< writes "succeed" but the value does not stick
    int writes = 0;

    Io io() {
        Io io;
        io.exists = [this](const std::string &p) { return nodes.count(p) > 0; };
        io.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = nodes.find(p);
            if (it == nodes.end()) return std::nullopt;
            return it->second + "\n";
        };
        io.write = [this](const std::string &p, const std::string &v) {
            ++writes;
            if (fail_write.count(p)) return false;
            if (!ignore_write.count(p)) nodes[p] = v;
            return true;
        };
        return io;
    }
};

RuntimePlan plan_of(FakeFs &fs, std::vector<std::pair<std::string, std::string>> writes) {
    RuntimePlan p;
    p.domain = "performance";
    p.subject = "com.example.game";
    for (auto &[path, value] : writes)
        p.operations.push_back(std::make_unique<NodeWriteOperation>(fs.io(), "memory", path, value));
    return p;
}

void test_snapshot_creation() {
    FakeFs fs;
    fs.nodes["/sys/a"] = "10";
    NodeWriteOperation op(fs.io(), "memory", "/sys/a", "20");
    CHECK(op.snapshot());
    CHECK_EQ(op.journal_entry(), journal::encode_entry("/sys/a", "10"));
    CHECK_EQ(fs.writes, 0); // snapshot never writes

    NodeWriteOperation missing(fs.io(), "memory", "/sys/missing", "1");
    CHECK(!missing.snapshot());
    CHECK(!missing.apply()); // never write a node that was not snapshotted
    CHECK_EQ(fs.nodes.count("/sys/missing"), size_t{0});

    NodeWriteOperation viewed(fs.io(), "storage", "/sys/sched", "none",
                              [](const std::string &raw) { return raw.substr(1, raw.find(']') - 1); });
    fs.nodes["/sys/sched"] = "[mq-deadline] none";
    CHECK(viewed.snapshot());
    CHECK_EQ(viewed.journal_entry(), journal::encode_entry("/sys/sched", "mq-deadline"));
}

void test_transaction_apply_and_verify_success() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}, {"/sys/b", "2"}};
    std::vector<std::string> journals;
    Transaction tx(make_transaction_id(1000, 7), plan_of(fs, {{"/sys/a", "10"}, {"/sys/b", "20"}, {"/sys/none", "5"}}),
                   [&](const std::string &j) { journals.push_back(j); return true; });
    CHECK_EQ(tx.id(), std::string("tx-1000-7"));
    CHECK(tx.start());
    CHECK(tx.state() == TxState::Active);
    CHECK_EQ(fs.nodes["/sys/a"], std::string("10"));
    CHECK_EQ(fs.nodes["/sys/b"], std::string("20"));
    CHECK_EQ(tx.skipped().size(), size_t{1}); // missing node skipped, not a failure
    CHECK(!journals.empty());
    // Write-ahead: the journal naming /sys/a existed before /sys/a was written.
    auto first = journal::parse(journals.front());
    CHECK_EQ(first.tx_id, std::string("tx-1000-7"));
    CHECK_EQ(first.entries.size(), size_t{1});
    CHECK_EQ(tx.journal_entries().size(), size_t{2});
}

void test_verify_failure_rolls_back() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}, {"/sys/b", "2"}};
    fs.ignore_write.insert("/sys/b"); // kernel clamps / rejects the value silently
    Transaction tx("tx-1", plan_of(fs, {{"/sys/a", "10"}, {"/sys/b", "20"}}));
    CHECK(!tx.start());
    CHECK(tx.state() == TxState::Failed);
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1")); // rolled back
    CHECK_EQ(fs.nodes["/sys/b"], std::string("2"));
}

void test_apply_failure_rolls_back() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}, {"/sys/b", "2"}};
    fs.fail_write.insert("/sys/b");
    Transaction tx("tx-2", plan_of(fs, {{"/sys/a", "10"}, {"/sys/b", "20"}}));
    CHECK(!tx.start());
    CHECK(tx.state() == TxState::Failed);
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1"));
}

void test_journal_sink_failure_blocks_apply() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}};
    Transaction tx("tx-3", plan_of(fs, {{"/sys/a", "10"}}), [](const std::string &) { return false; });
    CHECK(!tx.start());
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1")); // nothing applied without a journal
    CHECK_EQ(fs.writes, 0);
}

void test_restore_after_lifecycle() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}, {"/sys/b", "2"}};
    std::string last_journal = "unset";
    Transaction tx("tx-4", plan_of(fs, {{"/sys/a", "10"}, {"/sys/b", "20"}}),
                   [&](const std::string &j) { last_journal = j; return true; });
    CHECK(tx.start());
    fs.nodes["/sys/a"] = "99"; // profile script overwrote it
    CHECK(tx.reapply());
    CHECK_EQ(fs.nodes["/sys/a"], std::string("10"));
    CHECK(tx.finish());
    CHECK(tx.state() == TxState::Restored);
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1")); // original, not the overwritten value
    CHECK_EQ(fs.nodes["/sys/b"], std::string("2"));
    CHECK(journal::parse(last_journal).entries.empty()); // clean restore leaves an empty journal
    CHECK(tx.finish()); // idempotent
}

void test_restore_failure_is_reported() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}};
    Transaction tx("tx-5", plan_of(fs, {{"/sys/a", "10"}}));
    CHECK(tx.start());
    fs.fail_write.insert("/sys/a");
    CHECK(!tx.finish());
    CHECK(tx.state() == TxState::Failed);
    CHECK_EQ(tx.journal_entries().size(), size_t{1}); // kept for recovery
}

void test_crash_journal_recovery() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "1"}, {"/sys/b", "2"}};
    std::string journal_on_disk;
    {
        Transaction tx("tx-6", plan_of(fs, {{"/sys/a", "10"}, {"/sys/b", "20"}}),
                       [&](const std::string &j) { journal_on_disk = j; return true; });
        CHECK(tx.start());
        // daemon killed here: no finish()
    }
    CHECK_EQ(fs.nodes["/sys/a"], std::string("10"));
    RecoveryReport r = recover(fs.io(), journal_on_disk);
    CHECK_EQ(r.tx_id, std::string("tx-6"));
    CHECK_EQ(r.found, size_t{2});
    CHECK_EQ(r.restored, size_t{2});
    CHECK(r.clean());
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1"));
    CHECK_EQ(fs.nodes["/sys/b"], std::string("2"));

    // Legacy journal (old Game Runtime branch: entries only, no header) still recovers.
    fs.nodes["/sys/a"] = "10";
    RecoveryReport legacy = recover(fs.io(), journal::encode_entry("/sys/a", "1") + "\n");
    CHECK(legacy.clean());
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1"));

    // Old Game Runtime compat_journal: "#flux-compat-journal v1" header, then entries. Clean.
    fs.nodes["/sys/a"] = "10";
    RecoveryReport old = recover(fs.io(), "#flux-compat-journal v1\n" + journal::encode_entry("/sys/a", "1") + "\n");
    CHECK(old.clean());
    CHECK(old.corrupted.empty());
    CHECK_EQ(fs.nodes["/sys/a"], std::string("1"));

    // A restore that does not stick is reported as failed, so the journal is kept.
    fs.nodes["/sys/a"] = "10";
    fs.ignore_write.insert("/sys/a");
    RecoveryReport stuck = recover(fs.io(), journal::encode_entry("/sys/a", "1"));
    CHECK(!stuck.clean());
    CHECK_EQ(stuck.failed.size(), size_t{1});
}

void test_corrupted_journal_handling() {
    FakeFs fs;
    fs.nodes = {{"/sys/a", "10"}, {"/etc/passwd", "root"}};
    const std::string text = "flux-journal 1\ttx-7\tperformance\tgame\n"
                             "garbage-without-tab\n"
                             "relative/path\tx\n"
                             "/sys/../etc/passwd\towned\n"
                             "\tno-path\n" +
                             journal::encode_entry("/sys/a", "1") + "\n";
    int before = fs.writes;
    RecoveryReport r = recover(fs.io(), text);
    CHECK_EQ(r.corrupted.size(), size_t{4});
    CHECK_EQ(r.restored, size_t{1});
    CHECK(!r.clean()); // corrupted lines keep the journal for inspection
    CHECK_EQ(fs.nodes["/etc/passwd"], std::string("root"));
    CHECK_EQ(fs.writes - before, 1); // only the valid entry was written

    // Unknown future version: nothing is replayed.
    before = fs.writes;
    RecoveryReport future = recover(fs.io(), "flux-journal 9\ttx\td\ts\n" + journal::encode_entry("/sys/a", "1"));
    CHECK_EQ(future.restored, size_t{0});
    CHECK_EQ(future.corrupted.size(), size_t{1});
    CHECK_EQ(fs.writes, before);

    // Empty / whitespace journal is clean and writes nothing.
    CHECK(recover(fs.io(), "").clean());
    CHECK(recover(fs.io(), "\n\n").clean());

    // Escaping round-trips values with tabs, newlines and backslashes.
    std::string p, v;
    CHECK(journal::decode_entry(journal::encode_entry("/sys/x", "a\tb\nc\\d"), p, v));
    CHECK_EQ(v, std::string("a\tb\nc\\d"));
}

} // namespace

int main() {
    test_snapshot_creation();
    test_transaction_apply_and_verify_success();
    test_verify_failure_rolls_back();
    test_apply_failure_rolls_back();
    test_journal_sink_failure_blocks_apply();
    test_restore_after_lifecycle();
    test_restore_failure_is_reported();
    test_crash_journal_recovery();
    test_corrupted_journal_handling();
    return flux_test::report("transaction_test");
}
