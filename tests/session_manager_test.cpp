// Host tests for the session lifecycle (jni/session/SessionManager.*): ordering is checked with
// fake participants that record every call into one shared trace.

#include "flux_test.hpp"

#include "SessionManager.hpp"

#include <string>
#include <vector>

using namespace flux::session;

namespace {

struct Trace {
    std::vector<std::string> calls;
    std::string joined() const {
        std::string s;
        for (const auto &c : calls) s += (s.empty() ? "" : ",") + c;
        return s;
    }
};

struct Fake : SessionParticipant {
    Fake(const char *n, Trace &t) : n_(n), t_(t) {}
    const char *name() const override { return n_; }
    void recover() override { t_.calls.push_back(std::string(n_) + ".recover"); }
    void begin(const SessionInfo &s) override {
        t_.calls.push_back(std::string(n_) + ".begin(" + s.key.package + ":" + std::to_string(s.key.pid) + ")");
    }
    bool end(const SessionInfo &s, EndReason why) override {
        t_.calls.push_back(std::string(n_) + ".end(" + s.key.package + "," + to_string(why) + ")");
        return clean_end;
    }
    void profile_applied(const SessionInfo &) override { t_.calls.push_back(std::string(n_) + ".applied"); }
    void tick(const SessionInfo &, int64_t) override { ++ticks; }
    bool needs_tick() const override { return wants_tick; }
    bool clean_end = true;
    bool wants_tick = false;
    int ticks = 0;

private:
    const char *n_;
    Trace &t_;
};

SessionKey key(const std::string &pkg, int pid) { return {pkg, pid, 10000 + pid, {pid}}; }

void test_session_begin() {
    Trace t;
    Fake runtime("runtime", t), recorder("recorder", t);
    SessionManager m;
    m.add(&runtime);
    m.add(&recorder);
    m.recover(); // daemon start
    t.calls.clear();
    CHECK(!m.active());
    CHECK(m.begin(key("com.a", 100), 1000));
    CHECK(m.active());
    CHECK_EQ(m.current().key.package, std::string("com.a"));
    CHECK_EQ(m.current().started_ms, int64_t{1000});
    CHECK(m.current().id.rfind("s-1000-", 0) == 0);
    CHECK_EQ(t.joined(), std::string("runtime.begin(com.a:100),recorder.begin(com.a:100)"));
}

void test_duplicate_begin() {
    Trace t;
    Fake runtime("runtime", t), recorder("recorder", t);
    SessionManager m;
    m.add(&runtime);
    m.add(&recorder);
    m.recover();
    t.calls.clear();
    CHECK(m.begin(key("com.a", 100), 1000));
    const std::string id = m.current().id;
    CHECK(!m.begin(key("com.a", 100), 2000)); // same game, same process: nothing happens
    CHECK(!m.begin(key("com.a", 100), 3000));
    CHECK_EQ(m.current().id, id);
    CHECK_EQ(t.calls.size(), size_t{2});

    // Same game, restarted process: previous session ends (switch), a new one begins.
    CHECK(m.begin(key("com.a", 101), 4000));
    CHECK(m.current().id != id);
    CHECK_EQ(m.last().id, id);
    CHECK(m.last().end_reason == EndReason::Switch);

    // Different game: same.
    t.calls.clear();
    CHECK(m.begin(key("com.b", 200), 5000));
    CHECK_EQ(t.joined(), std::string("recorder.end(com.a,switch),runtime.end(com.a,switch),"
                                     "runtime.begin(com.b:200),recorder.begin(com.b:200)"));
}

void test_session_end_and_duplicate_end() {
    Trace t;
    Fake runtime("runtime", t), recorder("recorder", t);
    SessionManager m;
    m.add(&runtime);
    m.add(&recorder);
    CHECK(m.begin(key("com.a", 100), 1000));
    t.calls.clear();
    CHECK(m.end(EndReason::Exit, 9000));
    CHECK(!m.active());
    CHECK_EQ(m.last().ended_ms, int64_t{9000});
    CHECK(m.last().end_reason == EndReason::Exit);
    CHECK_EQ(t.joined(), std::string("recorder.end(com.a,exit),runtime.end(com.a,exit)"));

    t.calls.clear();
    CHECK(!m.end(EndReason::Exit, 9500)); // duplicate end: no participant is called
    CHECK(!m.end(EndReason::DaemonStop, 9600));
    CHECK(t.calls.empty());
    CHECK_EQ(m.last().ended_ms, int64_t{9000});
}

void test_process_death_end_reason() {
    Trace t;
    Fake runtime("runtime", t), recorder("recorder", t);
    SessionManager m;
    m.add(&runtime);
    m.add(&recorder);
    CHECK(m.begin(key("com.a", 100), 0));
    t.calls.clear();
    CHECK(m.end(EndReason::ProcessDeath, 10));
    CHECK(m.last().end_reason == EndReason::ProcessDeath);
    CHECK_EQ(t.joined(), std::string("recorder.end(com.a,process_death),runtime.end(com.a,process_death)"));
}

void test_daemon_restart_recovery() {
    Trace t;
    Fake runtime("runtime", t), recorder("recorder", t);
    SessionManager m;
    m.add(&runtime);
    m.add(&recorder);
    m.recover();
    m.recover(); // once per daemon run
    CHECK_EQ(t.joined(), std::string("runtime.recover,recorder.recover"));

    // begin() before recover() still recovers first: recovery can never be skipped.
    Trace t2;
    Fake r2("runtime", t2);
    SessionManager m2;
    m2.add(&r2);
    CHECK(m2.begin(key("com.a", 1), 0));
    CHECK_EQ(t2.joined(), std::string("runtime.recover,runtime.begin(com.a:1)"));
}

void test_runtime_cleanup_ordering() {
    Trace t;
    Fake runtime("runtime", t), recorder("recorder", t);
    recorder.clean_end = false; // an observer that fails to finish must not stop the restore
    SessionManager m;
    m.add(&runtime);
    m.add(&recorder);
    CHECK(m.begin(key("com.a", 100), 0));
    m.profile_applied();
    t.calls.clear();
    CHECK(!m.end(EndReason::DaemonStop, 5)); // reported not clean ...
    CHECK_EQ(t.joined(), std::string("recorder.end(com.a,daemon_stop),runtime.end(com.a,daemon_stop)"));
    CHECK(!m.active());                      // ... but the session is over and restore still ran

    // Profile re-application and ticks reach participants only during a session.
    t.calls.clear();
    m.profile_applied();
    runtime.wants_tick = true;
    CHECK(!m.needs_tick());
    m.tick(1);
    CHECK_EQ(runtime.ticks, 0);
    CHECK(m.begin(key("com.a", 100), 10));
    CHECK(m.needs_tick());
    m.tick(11);
    CHECK_EQ(runtime.ticks, 1);
    CHECK(!m.events().empty());
}

} // namespace

int main() {
    test_session_begin();
    test_duplicate_begin();
    test_session_end_and_duplicate_end();
    test_process_death_end_reason();
    test_daemon_restart_recovery();
    test_runtime_cleanup_ordering();
    return flux_test::report("session_manager_test");
}
