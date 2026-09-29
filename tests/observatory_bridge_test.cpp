// Host tests for the Observatory integration bridge: real SessionManager, GamePerformanceRuntime
// and Transaction Engine emitting into a MemoryEventStore through ObservatoryBridge.

#include "flux_test.hpp"

#include "ObservatoryBridge.hpp"

#include <map>
#include <set>
#include <stdexcept>

using namespace flux;
using observatory::Event;
using observatory::EventQuery;
using observatory::MemoryEventStore;

namespace {

constexpr int64_t kWall = 1790000000000;
const std::string kSwap = "/proc/sys/vm/swappiness";
const std::string kBoost = "/sys/module/cpu_boost/parameters/input_boost_ms";

struct World {
    std::map<std::string, std::string> nodes{{kSwap, "100"}, {kBoost, "0"}};
    std::map<std::string, std::string> files;
    std::set<std::string> fail_write;

    perf::RuntimeDeps deps() {
        perf::RuntimeDeps d;
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
        d.paths = {"/cfg/game_profiles.json", "", "/cfg/perf_journal", "/cfg/launch_journal", ""};
        d.caps.node_available = [this](const std::string &p) { return nodes.count(p) > 0; };
        return d;
    }
};

const char *kProfiles = R"({"version":1,"games":{"com.a":{"memory":"gaming","touch":"balanced"}}})";

/// A participant that forwards to a GamePerformanceRuntime, as the daemon glue does.
struct RuntimeParticipant : session::SessionParticipant {
    explicit RuntimeParticipant(perf::GamePerformanceRuntime &rt) : rt_(rt) {}
    const char *name() const override { return "game_runtime"; }
    void recover() override { rt_.recover(); }
    void begin(const session::SessionInfo &s) override { rt_.on_game_start(s.key.package, s.key.pid, s.started_ms); }
    bool end(const session::SessionInfo &, session::EndReason) override { return rt_.on_game_end(perf::EndReason::Exit); }
    perf::GamePerformanceRuntime &rt_;
};

struct Rig {
    World w;
    MemoryEventStore store{observatory::EventRegistry::builtin(), [] { return kWall; }};
    bridge::ObservatoryBridge br{&store, [] { return kWall; }};
    std::unique_ptr<perf::GamePerformanceRuntime> rt;
    std::unique_ptr<RuntimeParticipant> part;
    session::SessionManager sm;

    explicit Rig(observatory::EventSink *sink_override = nullptr, bool use_override = false) {
        if (use_override) br.set_sink(sink_override);
        w.files["/cfg/game_profiles.json"] = kProfiles;
        perf::RuntimeDeps d = w.deps();
        d.observer = br.runtime_observer();
        d.tx_observer = br.transaction_observer();
        rt = std::make_unique<perf::GamePerformanceRuntime>(d);
        part = std::make_unique<RuntimeParticipant>(*rt);
        sm.add(part.get());
        sm.set_observer(br.session_observer());
        sm.set_context(br.session_context());
    }
    std::vector<Event> of(const std::string &type) {
        EventQuery q;
        q.type = type;
        return store.query(q);
    }
    std::vector<std::string> types() {
        std::vector<std::string> t;
        for (const auto &e : store.query({})) t.push_back(e.type);
        return t;
    }
};

size_t index_of(const std::vector<std::string> &v, const std::string &t) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == t) return i;
    return v.size();
}

void test_event_emitted_on_session_start() {
    Rig r;
    CHECK(r.sm.begin({"com.a", 100, 10100, {100}}, kWall));
    auto starts = r.of("SESSION_START");
    CHECK_EQ(starts.size(), size_t{1});
    CHECK_EQ(starts[0].session_id, r.sm.current().id);
    CHECK_EQ(starts[0].after.at("package"), std::string("com.a"));
    CHECK(starts[0].result == observatory::Result::Ok);

    // Runtime and transaction events of that session carry its id and come before SESSION_START
    // (participants finish first), after the transitions they describe.
    auto t = r.types();
    CHECK(index_of(t, "TRANSACTION_BEGIN") < index_of(t, "TRANSACTION_APPLY"));
    CHECK(index_of(t, "TRANSACTION_APPLY") < index_of(t, "TRANSACTION_VERIFY"));
    CHECK(index_of(t, "TRANSACTION_VERIFY") < index_of(t, "PROFILE_APPLIED"));
    CHECK(index_of(t, "PROFILE_APPLIED") < index_of(t, "RUNTIME_ACTIVATE"));
    CHECK(index_of(t, "RUNTIME_ACTIVATE") < index_of(t, "SESSION_START"));
    const auto applied = r.of("PROFILE_APPLIED");
    CHECK_EQ(applied.size(), size_t{1});
    CHECK_EQ(applied[0].session_id, r.sm.current().id);
    const auto verifies = r.of("TRANSACTION_VERIFY");
    const Event verify = verifies.at(0);
    CHECK(!verify.transaction_id.empty());
    CHECK_EQ(verify.before.at(kSwap), std::string("100"));
    CHECK_EQ(verify.after.at(kSwap), std::string("60"));

    // Switch: END(switch) for the old session, START + SWITCH for the new one.
    const std::string first = r.sm.current().id;
    CHECK(r.sm.begin({"com.a", 101, 10101, {101}}, kWall + 10));
    auto sw = r.of("SESSION_SWITCH");
    CHECK_EQ(sw.size(), size_t{1});
    CHECK_EQ(sw[0].before.at("session_id"), first);
    const auto ends = r.of("SESSION_END");
    CHECK_EQ(ends.at(0).after.at("end_reason"), std::string("switch"));
}

void test_event_emitted_on_restore() {
    Rig r;
    CHECK(r.sm.begin({"com.a", 100, 10100, {100}}, kWall));
    const std::string sid = r.sm.current().id;
    CHECK(r.sm.end(session::EndReason::ProcessDeath, kWall + 5000));
    CHECK_EQ(r.w.nodes[kSwap], std::string("100"));
    auto restore = r.of("TRANSACTION_RESTORE");
    CHECK_EQ(restore.size(), size_t{1});
    CHECK(restore[0].result == observatory::Result::Ok);
    CHECK_EQ(restore[0].before.at(kSwap), std::string("60"));  // what was undone
    CHECK_EQ(restore[0].after.at(kSwap), std::string("100"));
    CHECK_EQ(r.of("PROFILE_RESTORED").size(), size_t{1});
    CHECK_EQ(r.of("RUNTIME_RESTORE").size(), size_t{1});
    auto end = r.of("SESSION_END");
    CHECK_EQ(end.size(), size_t{1});
    CHECK_EQ(end[0].session_id, sid);
    CHECK_EQ(end[0].after.at("end_reason"), std::string("process_death"));
    auto t = r.types();
    CHECK(index_of(t, "TRANSACTION_RESTORE") < index_of(t, "PROFILE_RESTORED"));
    CHECK(index_of(t, "RUNTIME_RESTORE") < index_of(t, "SESSION_END"));
    CHECK(r.br.session_id().empty()); // context cleared after the session
}

void test_transaction_failure_event() {
    Rig r;
    r.w.fail_write.insert(kBoost);
    CHECK(r.sm.begin({"com.a", 100, 10100, {100}}, kWall));
    auto apply = r.of("TRANSACTION_APPLY");
    CHECK_EQ(apply.size(), size_t{1});
    CHECK(apply[0].result == observatory::Result::Failed);
    CHECK(apply[0].reason.find(kBoost) != std::string::npos);
    auto rb = r.of("TRANSACTION_ROLLBACK");
    CHECK_EQ(rb.size(), size_t{1});
    // The failed node could not be written back either, so the engine reports the rollback as
    // incomplete (journal kept) even though that node never changed — recorded as B-27.
    CHECK(rb[0].reason.find("rollback") != std::string::npos);
    auto fail = r.of("RUNTIME_FAILURE");
    CHECK_EQ(fail.size(), size_t{1});
    CHECK(fail[0].severity >= observatory::Severity::Warning);
    CHECK(r.of("PROFILE_APPLIED").empty());
    CHECK(r.of("RUNTIME_ACTIVATE").empty());
    CHECK_EQ(r.w.nodes[kSwap], std::string("100"));
}

void test_recovery_event() {
    Rig r;
    r.w.nodes[kSwap] = "60";
    r.w.files["/cfg/perf_journal"] = runtime::journal::encode_entry(kSwap, "100") + "\n";
    r.sm.recover();
    auto t = r.types();
    CHECK(index_of(t, "RECOVERY_START") < index_of(t, "RECOVERY_SUCCESS"));
    const auto ok = r.of("RECOVERY_SUCCESS");
    CHECK_EQ(ok.at(0).after.at("restored"), std::string("1"));
    CHECK_EQ(r.w.nodes[kSwap], std::string("100"));

    Rig bad;
    bad.w.files["/cfg/perf_journal"] = "garbage\n";
    bad.sm.recover();
    auto failed = bad.of("RECOVERY_FAILED");
    CHECK_EQ(failed.size(), size_t{1});
    CHECK_EQ(failed[0].after.at("journal"), std::string("kept"));

    Rig none;
    none.sm.recover();
    CHECK_EQ(none.of("RECOVERY_SUCCESS").size(), size_t{1});
}

struct ThrowingSink : observatory::EventSink {
    observatory::WriteResult write(Event) override { throw std::runtime_error("disk full"); }
};
struct RejectingSink : observatory::EventSink {
    observatory::WriteResult write(Event) override { return {}; }
};

void test_observatory_unavailable_does_not_break_runtime() {
    // Reference run with a working Observatory.
    Rig ok;
    CHECK(ok.sm.begin({"com.a", 100, 10100, {100}}, kWall));
    const auto applied = ok.w.nodes;
    CHECK(ok.sm.end(session::EndReason::Exit, kWall + 1));

    ThrowingSink thrower;
    RejectingSink rejecter;
    for (observatory::EventSink *sink : {static_cast<observatory::EventSink *>(nullptr),
                                         static_cast<observatory::EventSink *>(&thrower),
                                         static_cast<observatory::EventSink *>(&rejecter)}) {
        Rig r(sink, true);
        r.sm.recover();
        CHECK(r.sm.begin({"com.a", 100, 10100, {100}}, kWall));
        CHECK(r.rt->state() == perf::RuntimeState::Active);
        CHECK(r.w.nodes == applied);                 // same values as with a working Observatory
        CHECK(r.sm.end(session::EndReason::Exit, kWall + 1));
        CHECK_EQ(r.w.nodes[kSwap], std::string("100"));
        CHECK(r.w.files.count("/cfg/perf_journal") == 0);
        CHECK_EQ(r.store.size(), size_t{0});
    }
    Rig t(&thrower, true);
    t.sm.begin({"com.a", 1, 1, {1}}, kWall);
    CHECK(t.br.stats().failed > 0);
    Rig n(nullptr, true);
    n.sm.begin({"com.a", 1, 1, {1}}, kWall);
    CHECK(n.br.stats().dropped > 0);

    // A throwing observer inside the engine itself is also contained.
    World w;
    w.files["/cfg/game_profiles.json"] = kProfiles;
    perf::RuntimeDeps d = w.deps();
    d.observer = [](const perf::RuntimeNotice &) { throw 1; };
    d.tx_observer = [](const runtime::TxNotice &) { throw 2; };
    perf::GamePerformanceRuntime rt(d);
    CHECK(rt.on_game_start("com.a", 1, 0));
    CHECK(rt.state() == perf::RuntimeState::Active);
    CHECK(rt.on_game_end(perf::EndReason::Exit));
    CHECK_EQ(w.nodes[kSwap], std::string("100"));
}

void test_events_are_valid_and_after_transition() {
    // Every emitted event passed validation (nothing rejected), and PROFILE_APPLIED is written
    // only once the node already holds the applied value.
    struct Spy : observatory::EventSink {
        World *w = nullptr;
        std::string seen;
        observatory::WriteResult write(Event e) override {
            if (e.type == "PROFILE_APPLIED") seen = w->nodes[kSwap];
            observatory::WriteResult r;
            r.accepted = true;
            return r;
        }
    } spy;
    Rig r(&spy, true);
    spy.w = &r.w;
    CHECK(r.sm.begin({"com.a", 100, 10100, {100}}, kWall));
    CHECK_EQ(spy.seen, std::string("60"));

    Rig v;
    v.sm.recover();
    v.sm.begin({"com.a", 100, 10100, {100}}, kWall);
    v.sm.end(session::EndReason::Exit, kWall + 1);
    CHECK_EQ(v.br.stats().rejected, size_t{0});
    CHECK(v.br.stats().emitted >= 12);
}

} // namespace

int main() {
    test_event_emitted_on_session_start();
    test_event_emitted_on_restore();
    test_transaction_failure_event();
    test_recovery_event();
    test_observatory_unavailable_does_not_break_runtime();
    test_events_are_valid_and_after_transition();
    return flux_test::report("observatory_bridge_test");
}
