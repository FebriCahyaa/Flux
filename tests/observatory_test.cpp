// Host tests for the Observatory event model and storage contract (jni/observatory/*).

#include "flux_test.hpp"

#include "EventStore.hpp"

#include <algorithm>

using namespace flux::observatory;

namespace {

constexpr int64_t kNow = 1790000000000; // 2026-09

bool has(const std::vector<std::string> &v, const std::string &needle) {
    return std::any_of(v.begin(), v.end(), [&](const std::string &s) { return s.find(needle) != std::string::npos; });
}

Event session_start(int64_t ts = kNow) {
    Event e;
    e.timestamp_ms = ts;
    e.source = "session";
    e.type = "SESSION_START";
    e.severity = Severity::Info;
    e.session_id = "s-1-1";
    e.reason = "game com.example.game detected in foreground";
    e.after = {{"package", "com.example.game"}, {"pid", "4242"}};
    e.confidence = Confidence::High;
    e.result = Result::Ok;
    return e;
}

Event tx_failed(int64_t ts = kNow) {
    Event e;
    e.timestamp_ms = ts;
    e.source = "transaction";
    e.type = "TRANSACTION_ROLLBACK";
    e.severity = Severity::Warning;
    e.session_id = "s-1-1";
    e.reason = "verify failed: /proc/sys/vm/swappiness read back 100, expected 60";
    e.before = {{"/proc/sys/vm/swappiness", "100"}};
    e.after = {{"/proc/sys/vm/swappiness", "100"}};
    e.confidence = Confidence::High;
    e.result = Result::Failed;
    e.transaction_id = "tx-5-2";
    return e;
}

void test_event_creation() {
    const EventRegistry reg = EventRegistry::builtin();
    for (const char *t : {"SESSION_START", "SESSION_END", "SESSION_SWITCH", "RUNTIME_ACTIVATE", "RUNTIME_RESTORE",
                          "RUNTIME_FAILURE", "PROFILE_APPLIED", "PROFILE_RESTORED", "TRANSACTION_BEGIN",
                          "TRANSACTION_APPLY", "TRANSACTION_VERIFY", "TRANSACTION_ROLLBACK", "TRANSACTION_RESTORE",
                          "RECOVERY_START", "RECOVERY_SUCCESS", "RECOVERY_FAILED"})
        CHECK(reg.find(t) != nullptr);
    CHECK(reg.find("SESSION_START")->category == Category::Session);
    CHECK(reg.find("RECOVERY_START")->category == Category::Recovery);

    CHECK(validate(session_start(), reg, kNow).empty());
    CHECK(validate(tx_failed(), reg, kNow).empty());

    MemoryEventStore store(reg, [] { return kNow; });
    WriteResult w = store.write(session_start());
    CHECK(w.accepted);
    CHECK(!w.event_id.empty());
    CHECK_EQ(w.sequence, uint64_t{1});
    CHECK_EQ(store.size(), size_t{1});

    // Registry refuses duplicates and malformed names.
    EventRegistry r2 = EventRegistry::builtin();
    CHECK(!r2.add({"SESSION_START", Category::Session, {"session"}, false, false, ""}));
    CHECK(!r2.add({"bad name", Category::Session, {"session"}, false, false, ""}));
    CHECK(r2.add({"SESSION_NOTE", Category::Session, {"session"}, false, false, "test"}));
}

void test_serialization() {
    Event e = tx_failed();
    e.event_id = "ev-1";
    e.sequence = 9;
    e.reason = "quote \" tab \t newline \n unicode ü";
    const std::string line = to_json(e);
    CHECK(line.find('\n') == std::string::npos); // one JSONL line
    Event back;
    std::string err;
    CHECK(from_json(line, back, err));
    CHECK_EQ(back.event_id, std::string("ev-1"));
    CHECK_EQ(back.timestamp_ms, e.timestamp_ms);
    CHECK_EQ(back.type, e.type);
    CHECK(back.severity == Severity::Warning);
    CHECK(back.confidence == Confidence::High);
    CHECK(back.result == Result::Failed);
    CHECK_EQ(back.reason, e.reason);
    CHECK_EQ(back.transaction_id, std::string("tx-5-2"));
    CHECK(back.before == e.before);
    CHECK(back.after == e.after);
    CHECK_EQ(back.sequence, uint64_t{9});
    CHECK(line.find("\"schema\":1") != std::string::npos);
}

void test_invalid_event_rejection() {
    const EventRegistry reg = EventRegistry::builtin();
    Event e = session_start();
    e.source.clear();
    CHECK(has(validate(e, reg, kNow), "source"));
    e = session_start();
    e.reason.clear();
    CHECK(has(validate(e, reg, kNow), "reason"));
    e = session_start();
    e.type = "NOT_A_TYPE";
    CHECK(has(validate(e, reg, kNow), "unknown event type"));
    e = session_start();
    e.source = "kernel"; // not an allowed producer of SESSION_START
    CHECK(has(validate(e, reg, kNow), "not allowed"));
    e = session_start();
    e.session_id.clear();
    CHECK(has(validate(e, reg, kNow), "session_id"));
    e = tx_failed();
    e.transaction_id.clear();
    CHECK(has(validate(e, reg, kNow), "transaction_id"));

    // Invalid severity / confidence / result only exist as text: rejected at parse time.
    Event p;
    std::string err;
    std::string line = to_json(session_start());
    auto swap = [&](const std::string &from, const std::string &to) {
        std::string s = line;
        s.replace(s.find(from), from.size(), to);
        return s;
    };
    CHECK(!from_json(swap("\"severity\":\"info\"", "\"severity\":\"loud\""), p, err));
    CHECK(err.find("severity") != std::string::npos);
    CHECK(!from_json(swap("\"confidence\":\"high\"", "\"confidence\":\"sure\""), p, err));
    CHECK(!from_json(swap("\"result\":\"ok\"", "\"result\":\"yay\""), p, err));

    // Rejected events are not stored.
    MemoryEventStore store(reg, [] { return kNow; });
    Event bad = session_start();
    bad.type = "NOT_A_TYPE";
    WriteResult w = store.write(bad);
    CHECK(!w.accepted);
    CHECK(!w.errors.empty());
    CHECK_EQ(store.size(), size_t{0});
    CHECK_EQ(store.rejected(), size_t{1});
}

void test_timestamp_validation() {
    const EventRegistry reg = EventRegistry::builtin();
    CHECK(has(validate(session_start(0), reg, kNow), "timestamp"));
    CHECK(has(validate(session_start(-5), reg, kNow), "timestamp"));
    CHECK(has(validate(session_start(1000), reg, kNow), "timestamp"));                  // 1970: broken clock
    CHECK(has(validate(session_start(kNow + 2 * 86400000LL), reg, kNow), "future"));   // beyond skew
    CHECK(validate(session_start(kNow + 3600000), reg, kNow).empty());                 // small skew ok
    Event p;
    std::string err;
    std::string line = to_json(session_start());
    line.replace(line.find("\"timestamp_ms\":"), 15, "\"timestamp_ms\":\"x\",\"z\":");
    CHECK(!from_json(line, p, err));
}

void test_ordering() {
    MemoryEventStore store(EventRegistry::builtin(), [] { return kNow; });
    CHECK(store.write(session_start(kNow - 100)).accepted);
    CHECK(store.write(tx_failed(kNow - 300)).accepted);   // arrives later, happened earlier
    CHECK(store.write(tx_failed(kNow - 100)).accepted);   // same timestamp as the first
    auto all = store.query({});
    CHECK_EQ(all.size(), size_t{3});
    CHECK_EQ(all[0].timestamp_ms, kNow - 300);
    CHECK_EQ(all[1].type, std::string("SESSION_START"));   // equal timestamp: write order
    CHECK_EQ(all[2].type, std::string("TRANSACTION_ROLLBACK"));
    CHECK(all[1].sequence < all[2].sequence);

    EventQuery q;
    q.category = Category::Transaction;
    q.min_severity = Severity::Warning;
    CHECK_EQ(store.query(q).size(), size_t{2});
    q = {};
    q.transaction_id = "tx-5-2";
    q.from_ms = kNow - 150;
    CHECK_EQ(store.query(q).size(), size_t{1});
    q = {};
    q.limit = 1;
    CHECK_EQ(store.query(q)[0].timestamp_ms, kNow - 300);

    // Bounded: the oldest event drops first.
    MemoryEventStore small(EventRegistry::builtin(), [] { return kNow; }, 2);
    small.write(session_start(kNow - 3));
    small.write(session_start(kNow - 2));
    small.write(session_start(kNow - 1));
    CHECK_EQ(small.size(), size_t{2});
    CHECK_EQ(small.dropped(), size_t{1});
    CHECK_EQ(small.query({})[0].timestamp_ms, kNow - 2);
}

void test_corrupted_event_handling() {
    Event p;
    std::string err;
    CHECK(!from_json("", p, err));
    CHECK(!from_json("{not json", p, err));
    CHECK(!from_json("[1,2]", p, err));
    CHECK(!from_json(R"({"schema":1})", p, err));                         // missing fields
    std::string line = to_json(session_start());
    CHECK(!from_json(line.substr(0, line.size() / 2), p, err));           // truncated write
    std::string extra = line;
    extra.insert(extra.size() - 1, ",\"injected\":true");
    CHECK(!from_json(extra, p, err));                                    // unknown key
    std::string future = line;
    future.replace(future.find("\"schema\":1"), 10, "\"schema\":9");
    CHECK(!from_json(future, p, err));

    const std::string text = line + "\n" + "garbage\n\n" + to_json(tx_failed()) + "\n" + line.substr(0, 20) + "\n";
    ParsedLines parsed = parse_jsonl(text);
    CHECK_EQ(parsed.events.size(), size_t{2});
    CHECK_EQ(parsed.corrupted.size(), size_t{2});
    CHECK(has(parsed.corrupted, "line 2"));
    CHECK(has(parsed.corrupted, "line 5"));
}

} // namespace

int main() {
    test_event_creation();
    test_serialization();
    test_invalid_event_rejection();
    test_timestamp_validation();
    test_ordering();
    test_corrupted_event_handling();
    return flux_test::report("observatory_test");
}
