// Observatory historical analysis (Step 8.12): session reconstruction, transaction / bottleneck /
// thermal / FPS explanations, missing and contradictory evidence, corruption, bounded history,
// determinism, read-only guarantees (library and CLI).
#include "flux_test.hpp"
#include "BottleneckEvents.hpp"
#include "ObservatoryAnalyzer.hpp"
#include "TelemetryCli.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace b = flux::bottleneck;
namespace ctx = flux::context;
namespace fs = std::filesystem;
using namespace flux::observatory;

namespace {

constexpr int64_t kHour = 3600LL * 1000, kDay = 24 * kHour;
int64_t wall = 1'760'000'000'000;
TelemetryClock clock_() { return {[] { return wall; }, [] { return int64_t(1); }}; }

struct World {
    fs::path root;
    PersistentEventStore store;
    uint64_t seq = 0;
    explicit World(const std::string &tag)
        : root(fs::temp_directory_path() / ("flux_ana_" + tag + "_" + std::to_string(::getpid()))),
          store((fs::remove_all(root), fs::create_directories(root), root.string()), clock_(), make_posix_io()) {
        store.open();
    }
    ~World() { fs::remove_all(root); }
    Event put(Event e) {
        e.sequence = ++seq;
        e.event_id = "ev-" + std::to_string(e.timestamp_ms) + "-" + std::to_string(e.sequence);
        if (e.reason.empty()) e.reason = "test";
        CHECK(store.append(e));
        return e;
    }
    Event ev(const std::string &type, const std::string &source, int64_t ts, const std::string &sid,
             std::map<std::string, std::string> after = {}, Result result = Result::Ok, const std::string &tx = "") {
        Event e;
        e.type = type;
        e.source = source;
        e.timestamp_ms = ts;
        e.session_id = sid;
        e.after = std::move(after);
        e.result = result;
        e.transaction_id = tx;
        e.confidence = Confidence::High;
        if (result != Result::Ok) e.severity = Severity::Warning;
        return put(e);
    }
    std::string events_snapshot() const {
        std::ostringstream out;
        for (auto &d : fs::recursive_directory_iterator(root))
            if (d.is_regular_file())
                out << d.path().string() << " " << d.file_size() << " "
                    << d.last_write_time().time_since_epoch().count() << "\n";
        return out.str();
    }
};

b::Assessment assessment(void (*fill)(b::RuntimeSample &), double fps, const b::ThermalContext *thermal = nullptr,
                         const ctx::CapabilityContext *c = nullptr, bool with_target = true) {
    b::BottleneckInputs in;
    for (int i = 0; i < 6; ++i) {
        b::RuntimeSample s;
        s.timestamp_ms = 1000 + i * 1000;
        s.fps = fps;
        if (with_target) s.target_hz = 60;
        fill(s);
        in.session.samples.push_back(s);
    }
    in.thermal = thermal;
    in.context = c;
    in.now_ms = 9000;
    return b::assess(in);
}
void cpu_fill(b::RuntimeSample &s) {
    s.cpu_busiest_core = 0.98;
    s.cpu_freq_ratio = 1.0;
    s.gpu_busy = 0.4;
}
void both_fill(b::RuntimeSample &s) {
    s.cpu_busiest_core = 0.97;
    s.cpu_freq_ratio = 1.0;
    s.gpu_busy = 0.97;
}
void idle_fill(b::RuntimeSample &s) {
    s.cpu_busiest_core = 0.4;
    s.gpu_busy = 0.4;
}
struct Hot : b::ThermalContext {
    std::optional<b::ThermalState> at(int64_t t) const override { return b::ThermalState{t, true, std::nullopt, "synrei"}; }
};

Event bottleneck(World &w, const std::string &sid, int64_t ts, const b::Assessment &a) {
    return w.put(flux::bridge::bottleneck_event(b::make_result(a, sid), ts));
}

/// A full session the way fluxd records it.
void normal_session(World &w, const std::string &sid, const std::string &pkg, int64_t t0,
                    const std::string &end_reason = "exit", const b::Assessment *a = nullptr,
                    Result restore = Result::Ok) {
    const std::string tx = "tx-" + sid;
    w.ev("SESSION_START", "session", t0, sid, {{"package", pkg}, {"pid", "100"}});
    w.ev("RUNTIME_ACTIVATE", "game_runtime", t0 + 10, sid, {{"package", pkg}});
    w.ev("TRANSACTION_BEGIN", "transaction", t0 + 20, sid, {{"subject", "game"}}, Result::Ok, tx);
    w.ev("TRANSACTION_APPLY", "transaction", t0 + 30, sid, {{"subject", "game"}}, Result::Ok, tx);
    w.ev("TRANSACTION_VERIFY", "transaction", t0 + 40, sid, {{"subject", "game"}}, Result::Ok, tx);
    w.ev("PROFILE_APPLIED", "performance", t0 + 50, sid, {{"package", pkg}});
    if (a) bottleneck(w, sid, t0 + 60000, *a);
    std::map<std::string, std::string> rs = {{"subject", "game"},
                                             {"restored", restore == Result::Ok ? "3" : "2"},
                                             {"not_restored", restore == Result::Ok ? "0" : "1"}};
    w.ev("TRANSACTION_RESTORE", "transaction", t0 + 60010, sid, rs, restore, tx);
    w.ev("RUNTIME_RESTORE", "game_runtime", t0 + 60020, sid, {{"package", pkg}}, restore == Result::Ok ? Result::Ok : Result::Partial);
    w.ev("SESSION_END", "session", t0 + 60030, sid,
         {{"end_reason", end_reason}, {"duration_ms", "60030"}, {"clean", restore == Result::Ok ? "true" : "false"}},
         restore == Result::Ok ? Result::Ok : Result::Partial);
}

const Explanation *topic(const SessionAnalysis &a, const std::string &t) {
    for (auto &e : a.explanations)
        if (e.topic == t) return &e;
    return nullptr;
}
bool any_contains(const std::vector<std::string> &v, const std::string &needle) {
    for (auto &s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}
bool no_causal_claims(const std::string &text) {
    for (auto bad : {"definitely", "guaranteed", "proven", "caused by", "causes "})
        if (text.find(bad) != std::string::npos) return false;
    return true;
}

void test_empty_session() {
    World w("empty");
    ObservatoryAnalyzer an(w.store);
    auto a = an.analyze("s-missing");
    CHECK(a.timeline.entries.empty());
    CHECK(!a.timeline.complete());
    CHECK(any_contains(a.timeline.limitations, "no events recorded"));
    CHECK(!a.summary.duration_ms); // unknown, never zero
    CHECK_EQ(a.summary.end_reason, std::string("unknown"));
    CHECK_EQ(a.summary.primary_bottleneck, std::string("unknown"));
}

void test_normal_reconstruction() {
    World w("normal");
    auto cpu = assessment(cpu_fill, 40);
    normal_session(w, "s-1", "com.game", wall - kHour, "exit", &cpu);
    ObservatoryAnalyzer an(w.store);
    auto t = an.timeline("s-1");
    CHECK(t.complete() && t.has_profile && t.has_runtime && t.has_transactions && t.has_bottleneck);
    CHECK_EQ(t.package, std::string("com.game"));
    CHECK_EQ(t.end_reason, std::string("exit"));
    CHECK(t.duration_ms && *t.duration_ms == 60030);
    CHECK(t.clean_end && *t.clean_end);
    CHECK_EQ(t.entries.size(), size_t(10));
    CHECK(t.entries.front().phase == Phase::Start && t.entries.back().phase == Phase::End);
    for (size_t i = 1; i < t.entries.size(); ++i) // EventStore order preserved
        CHECK(t.entries[i - 1].event.timestamp_ms < t.entries[i].event.timestamp_ms ||
              (t.entries[i - 1].event.timestamp_ms == t.entries[i].event.timestamp_ms &&
               t.entries[i - 1].event.sequence < t.entries[i].event.sequence));
    CHECK(t.transaction_ids.size() == 1 && t.transaction_ids[0] == "tx-s-1");
    CHECK(t.limitations.empty());
    auto a = an.analyze("s-1");
    CHECK_EQ(a.summary.package, std::string("com.game"));
    CHECK(a.summary.completeness.find("complete") == 0);
    CHECK(topic(a, "lifecycle") && topic(a, "transaction") && topic(a, "bottleneck") && topic(a, "thermal") &&
          topic(a, "fps"));
    CHECK(no_causal_claims(to_text(a)));
}

void test_end_reasons_and_switch() {
    World w("ends");
    normal_session(w, "s-pd", "com.a", wall - 5 * kHour, "process_death");
    normal_session(w, "s-fl", "com.a", wall - 4 * kHour, "focus_lost");
    // Switch: old ends with "switch", the new one starts and records SESSION_SWITCH.
    const int64_t t = wall - 3 * kHour;
    w.ev("SESSION_START", "session", t, "s-old", {{"package", "com.a"}});
    w.ev("SESSION_END", "session", t + 1000, "s-old", {{"end_reason", "switch"}, {"duration_ms", "1000"}, {"clean", "true"}});
    w.ev("SESSION_START", "session", t + 1000, "s-new", {{"package", "com.b"}});
    auto sw = w.ev("SESSION_SWITCH", "session", t + 1000, "s-new", {{"session_id", "s-new"}, {"package", "com.b"}});
    ObservatoryAnalyzer an(w.store);
    CHECK_EQ(an.analyze("s-pd").summary.end_reason, std::string("process_death"));
    CHECK_EQ(an.analyze("s-fl").summary.end_reason, std::string("focus_lost"));
    CHECK_EQ(an.timeline("s-old").end_reason, std::string("switch"));
    (void)sw;
    // previous_session_id comes from the SWITCH event's `before`; set it as the bridge does.
    World w2("switch");
    w2.ev("SESSION_START", "session", t, "s-old", {{"package", "com.a"}});
    w2.ev("SESSION_END", "session", t + 1000, "s-old", {{"end_reason", "switch"}, {"duration_ms", "1000"}, {"clean", "true"}});
    w2.ev("SESSION_START", "session", t + 1000, "s-new", {{"package", "com.b"}});
    Event s;
    s.type = "SESSION_SWITCH";
    s.source = "session";
    s.timestamp_ms = t + 1000;
    s.session_id = "s-new";
    s.before = {{"session_id", "s-old"}};
    s.after = {{"session_id", "s-new"}, {"package", "com.b"}};
    w2.put(s);
    ObservatoryAnalyzer an2(w2.store);
    auto nt = an2.timeline("s-new");
    CHECK_EQ(nt.previous_session_id, std::string("s-old"));
    CHECK(nt.has_start && !nt.has_end);
    CHECK(any_contains(nt.limitations, "SESSION_END"));
}

void test_transactions() {
    World w("tx");
    normal_session(w, "s-ok", "com.a", wall - 6 * kHour);
    // Rollback: apply failed, rollback restored everything.
    const int64_t t = wall - 5 * kHour;
    w.ev("SESSION_START", "session", t, "s-rb", {{"package", "com.a"}});
    w.ev("TRANSACTION_BEGIN", "transaction", t + 1, "s-rb", {}, Result::Ok, "tx-rb");
    w.ev("TRANSACTION_APPLY", "transaction", t + 2, "s-rb", {{"subject", "game"}}, Result::Failed, "tx-rb");
    w.ev("TRANSACTION_ROLLBACK", "transaction", t + 3, "s-rb", {{"restored", "2"}, {"not_restored", "0"}}, Result::Ok, "tx-rb");
    w.ev("SESSION_END", "session", t + 4, "s-rb", {{"end_reason", "failure"}, {"duration_ms", "4"}, {"clean", "true"}});
    // Restore partial.
    normal_session(w, "s-part", "com.a", wall - 4 * kHour, "exit", nullptr, Result::Partial);
    ObservatoryAnalyzer an(w.store);

    auto ok = an.analyze("s-ok");
    auto *e = topic(ok, "transaction");
    CHECK(e && e->related_transaction_id == "tx-s-ok");
    CHECK(e && e->summary.find("applied") != std::string::npos && e->summary.find("verified") != std::string::npos &&
          e->summary.find("restored") != std::string::npos);
    CHECK(e && e->confidence == Confidence::High && e->supporting.size() >= 4 && e->contradicting.empty());
    CHECK(ok.summary.restore_status.find("restored") != std::string::npos);

    auto rb = an.analyze("s-rb");
    auto *r = topic(rb, "transaction");
    CHECK(r && r->summary.find("apply failed") != std::string::npos);
    CHECK(r && r->summary.find("rollback") != std::string::npos);
    // The failed apply is never claimed as verified (the rollback's own read-back is real evidence).
    CHECK(r && r->summary.find("verified by read-back") == std::string::npos);
    CHECK(rb.summary.transaction_status.find("verified") == std::string::npos);
    CHECK(rb.summary.transaction_status.find("rolled back") != std::string::npos);

    auto part = an.analyze("s-part");
    auto *p = topic(part, "transaction");
    CHECK(p && p->summary.find("partial") != std::string::npos);
    CHECK(part.summary.restore_status.find("1 node(s) not restored") != std::string::npos);
    CHECK(part.timeline.clean_end && !*part.timeline.clean_end);
}

void test_recovery() {
    World w("recovery");
    const int64_t t = wall - 2 * kHour;
    w.ev("SESSION_START", "session", t, "s-r", {{"package", "com.a"}});
    w.ev("RECOVERY_START", "recovery", t + 5, "");
    w.ev("RECOVERY_FAILED", "recovery", t + 6, "", {}, Result::Partial);
    w.ev("RECOVERY_START", "recovery", t + 99999, ""); // after the session: not attached
    w.ev("SESSION_END", "session", t + 10, "s-r", {{"end_reason", "exit"}, {"duration_ms", "10"}, {"clean", "true"}});
    ObservatoryAnalyzer an(w.store);
    auto a = an.analyze("s-r");
    int ctx_entries = 0;
    for (auto &x : a.timeline.entries)
        if (!x.in_session) {
            ++ctx_entries;
            CHECK(x.phase == Phase::Recovery);
        }
    CHECK_EQ(ctx_entries, 2);
    auto *rec = topic(a, "recovery");
    CHECK(rec && rec->summary.find("recovery failed") != std::string::npos);
    CHECK(rec && any_contains(rec->limitations, "no session_id"));
}

void test_bottleneck_explanation() {
    World w("bottleneck");
    auto cpu = assessment(cpu_fill, 40);
    normal_session(w, "s-b", "com.a", wall - kHour, "exit", &cpu);
    ObservatoryAnalyzer an(w.store);
    auto a = an.analyze("s-b");
    auto *e = topic(a, "bottleneck");
    CHECK(e && e->primary_finding == "cpu confirmed");
    CHECK(e && e->confidence == Confidence::High);
    CHECK_EQ(a.summary.primary_bottleneck, std::string("cpu"));
    CHECK_EQ(a.summary.bottleneck_rating, std::string("confirmed"));
    CHECK_EQ(a.summary.bottleneck_confidence, std::string("high"));
    // Evidence references point at the stored event and carry the stored value exactly.
    const Event *stored = nullptr;
    for (auto &x : a.timeline.entries)
        if (x.event.type == "BOTTLENECK_ASSESSED") stored = &x.event;
    CHECK(stored != nullptr);
    bool exact = false;
    for (auto &ref : e->supporting)
        if (stored && ref.event_id == stored->event_id && ref.field == "after.evidence.cpu.0")
            exact = ref.value == stored->after.at("evidence.cpu.0");
    CHECK(exact);
    // Preserved exactly: same `after` as the producer wrote.
    auto again = flux::bridge::bottleneck_event(b::make_result(cpu, "s-b"), 0);
    if (stored) CHECK(stored->after == again.after);
    CHECK(no_causal_claims(e->summary));
}

void test_thermal_and_stale() {
    World w("thermal");
    Hot hot;
    auto th = assessment(cpu_fill, 40, &hot);
    normal_session(w, "s-hot", "com.a", wall - 2 * kHour, "exit", &th);
    auto plain = assessment(cpu_fill, 40); // no thermal evidence
    normal_session(w, "s-cool", "com.a", wall - kHour, "exit", &plain);
    ObservatoryAnalyzer an(w.store);
    auto hotA = an.analyze("s-hot");
    auto *t = topic(hotA, "thermal");
    CHECK(t && t->primary_finding.find("thermal constraint") != std::string::npos);
    CHECK(t && t->summary.find("synrei") != std::string::npos);
    CHECK(t && t->summary.find("correlated") != std::string::npos); // with the CPU saturation finding
    CHECK(t && no_causal_claims(t->summary));
    CHECK(t && any_contains(t->limitations, "state transitions are not recorded"));
    CHECK(hotA.summary.thermal.find("constraint") != std::string::npos);

    auto cool = an.analyze("s-cool");
    auto *c = topic(cool, "thermal");
    CHECK(c && c->primary_finding == "unknown");
    CHECK(c && c->confidence == Confidence::Unknown);
    CHECK(c && any_contains(c->limitations, "stale")); // missing / stale / unverified are reported, not hidden
    CHECK(c && c->summary.find("not inferred from temperature") != std::string::npos);
}

void test_fps() {
    World w("fps");
    auto cpu = assessment(cpu_fill, 40);
    normal_session(w, "s-short", "com.a", wall - 2 * kHour, "exit", &cpu);
    ctx::CapabilityContext c;
    auto fact = [](const std::string &id, const std::string &v) {
        ctx::CapabilityFact f;
        f.id = id;
        f.domain = "display";
        f.support = ctx::Support::Yes;
        f.readable = true;
        f.confidence = ctx::Confidence::High;
        f.value = v;
        return f;
    };
    c.publish("display", {fact("display.refresh.current_hz", "60"), fact("display.refresh.max_hz", "120")});
    auto disp = assessment(idle_fill, 59.9, nullptr, &c);
    normal_session(w, "s-disp", "com.a", wall - kHour, "exit", &disp);
    ObservatoryAnalyzer an(w.store);

    auto s = an.analyze("s-short").summary.fps;
    CHECK(s.shortfall_samples && *s.shortfall_samples == 6 && s.fps_samples && *s.fps_samples == 6);
    CHECK(!s.target_refresh_hz); // not recorded in this assessment's evidence: unknown, not 60 or 0
    CHECK(any_contains(s.limitations, "target refresh"));
    auto *f = topic(an.analyze("s-short"), "fps");
    CHECK(f && f->summary.find("below target") != std::string::npos);

    auto d = an.analyze("s-disp").summary.fps;
    CHECK(d.target_refresh_hz && *d.target_refresh_hz == 60);
    CHECK(d.refresh_capability_hz && *d.refresh_capability_hz == 120);
    CHECK(d.observed_fps_peak && *d.observed_fps_peak > 59 && *d.observed_fps_peak < 60);
    auto *fd = topic(an.analyze("s-disp"), "fps");
    CHECK(fd && fd->summary.find("Hz") != std::string::npos && fd->summary.find("FPS") != std::string::npos);
    CHECK(fd && fd->summary.find("refresh is not frame rate") != std::string::npos);
}

void test_contradictory_and_missing() {
    World w("contra");
    auto both = assessment(both_fill, 40);
    normal_session(w, "s-c", "com.a", wall - 3 * kHour, "exit", &both);
    // Apply ok, verify failed: contradictory transaction evidence.
    const int64_t t = wall - 2 * kHour;
    w.ev("SESSION_START", "session", t, "s-v", {{"package", "com.a"}});
    w.ev("TRANSACTION_APPLY", "transaction", t + 1, "s-v", {}, Result::Ok, "tx-v");
    w.ev("TRANSACTION_VERIFY", "transaction", t + 2, "s-v", {}, Result::Failed, "tx-v");
    // Missing evidence: start only.
    w.ev("SESSION_START", "session", wall - kHour, "s-m", {{"package", "com.a"}});
    ObservatoryAnalyzer an(w.store);

    auto c = an.analyze("s-c");
    auto *bn = topic(c, "bottleneck");
    CHECK(bn && bn->primary_finding.find("unknown") == 0);
    CHECK(bn && bn->contradicting.size() >= 2);
    CHECK(bn && any_contains(bn->limitations, "conflict"));

    auto v = an.analyze("s-v");
    auto *tx = topic(v, "transaction");
    CHECK(tx && !tx->contradicting.empty());
    CHECK(tx && tx->summary.find("verification failed") != std::string::npos);
    CHECK(tx && any_contains(tx->limitations, "TRANSACTION_RESTORE"));

    auto m = an.analyze("s-m");
    CHECK(!m.timeline.complete());
    CHECK(any_contains(m.timeline.limitations, "SESSION_END"));
    CHECK(any_contains(m.summary.limitations, "BOTTLENECK_ASSESSED"));
    CHECK(!m.summary.duration_ms);
    CHECK(m.summary.completeness.find("incomplete") == 0);
    CHECK_EQ(m.summary.primary_bottleneck, std::string("unknown"));
}

void test_corrupt_input() {
    World w("corruptin");
    normal_session(w, "s-x", "com.a", wall - kHour);
    for (auto &d : fs::directory_iterator(w.root / "telemetry/events"))
        if (d.path().extension() == ".jsonl") std::ofstream(d.path(), std::ios::app) << "garbage line\n{\"half\":\n";
    const auto before = w.events_snapshot();
    ObservatoryAnalyzer an(w.store);
    auto a = an.analyze("s-x");
    CHECK(a.timeline.complete());
    CHECK(a.corrupted_lines >= 2);
    CHECK(any_contains(a.summary.limitations, "corrupt"));
    CHECK_EQ(w.events_snapshot(), before); // never repaired / rewritten
}

void test_history() {
    World w("history");
    auto cpu = assessment(cpu_fill, 40);
    // An old session (written when it was fresh), outside the 7-day window at analysis time.
    const int64_t now = wall;
    wall = now - 9 * kDay;
    normal_session(w, "s-old", "com.g", wall, "exit", &cpu);
    wall = now;
    for (int i = 0; i < 3; ++i) normal_session(w, "s-c" + std::to_string(i), "com.g", now - (5 - i) * kHour, "exit", &cpu);
    normal_session(w, "s-f1", "com.g", now - 2 * kHour + 100, "exit", nullptr, Result::Failed);
    normal_session(w, "s-f2", "com.g", now - kHour, "exit", nullptr, Result::Failed);
    normal_session(w, "s-other", "com.other", now - kHour + 500, "exit", &cpu);
    ObservatoryAnalyzer an(w.store);
    auto h = an.history("com.g", now - 30 * kDay, now);
    CHECK(h.range.from_ms && *h.range.from_ms == now - kRetentionMs);
    CHECK(any_contains(h.limitations, "clamped"));
    CHECK_EQ(h.sessions, 5); // s-old excluded, com.other excluded
    CHECK_EQ(h.sessions_with_assessment, 3);
    const Pattern *cpu_p = nullptr, *restore_p = nullptr, *gpu_p = nullptr;
    for (auto &p : h.patterns) {
        if (p.name == "repeated_cpu_bottleneck") cpu_p = &p;
        if (p.name == "repeated_restore_failure") restore_p = &p;
        if (p.name == "repeated_gpu_bottleneck") gpu_p = &p;
    }
    CHECK(cpu_p && cpu_p->occurrences == 3 && cpu_p->sessions_considered == 3);
    CHECK(cpu_p && cpu_p->range.from_ms && cpu_p->range.to_ms && *cpu_p->range.from_ms < *cpu_p->range.to_ms);
    CHECK(cpu_p && any_contains(cpu_p->limitations, "without a bottleneck assessment")); // incomparable excluded
    CHECK(restore_p && restore_p->occurrences == 2);
    CHECK(gpu_p == nullptr); // nothing reported without occurrences
    for (auto &p : h.patterns) CHECK(p.confidence != Confidence::High); // repetition is not causation
}

void test_determinism_and_read_only() {
    World w("determinism");
    auto cpu = assessment(cpu_fill, 40);
    normal_session(w, "s-d", "com.a", wall - 2 * kHour, "exit", &cpu);
    normal_session(w, "s-e", "com.a", wall - kHour, "process_death", &cpu, Result::Partial);
    const auto before = w.events_snapshot();
    ObservatoryAnalyzer an(w.store);
    auto t1 = to_text(an.analyze("s-d"));
    auto h1 = to_text(an.history("com.a", wall - kDay, wall));
    // A fresh read-only store over the same files gives identical output.
    PersistentEventStore ro(w.root.string(), {[] { return int64_t(0); }, [] { return int64_t(0); }}, make_posix_io());
    CHECK(ro.open(true));
    ObservatoryAnalyzer an2(ro);
    CHECK_EQ(to_text(an2.analyze("s-d")), t1);
    CHECK_EQ(to_text(an2.history("com.a", wall - kDay, wall)), h1);
    CHECK_EQ(w.events_snapshot(), before); // no runtime / telemetry mutation

    // Read-only CLI: every command leaves the store untouched.
    std::ostringstream out, err;
    const std::string root = w.root.string();
    CHECK_EQ(run_telemetry_command({"session", "s-d"}, root, out, err), 0);
    CHECK(out.str().find("SESSION_START") != std::string::npos);
    out.str("");
    CHECK_EQ(run_telemetry_command({"analyze", "s-d"}, root, out, err), 0);
    CHECK(out.str().find("cpu confirmed") != std::string::npos);
    out.str("");
    CHECK_EQ(run_telemetry_command({"history", "com.a"}, root, out, err), 0); // ends at the newest stored event
    CHECK(out.str().find("sessions: 2") != std::string::npos);
    std::ostringstream out2;
    run_telemetry_command({"history", "com.a"}, root, out2, err);
    CHECK_EQ(out2.str(), out.str());
    CHECK_EQ(run_telemetry_command({"status"}, root, out, err), 0);
    CHECK_EQ(run_telemetry_command({"retention"}, root, out, err), 0);
    CHECK_EQ(run_telemetry_command({"query", "type=SESSION_END"}, root, out, err), 0);
    CHECK_EQ(run_telemetry_command({"analyze"}, root, out, err), 1);
    CHECK_EQ(run_telemetry_command({"bogus"}, root, out, err), 1);
    CHECK_EQ(w.events_snapshot(), before);
}

} // namespace

int main() {
    test_empty_session();
    test_normal_reconstruction();
    test_end_reasons_and_switch();
    test_transactions();
    test_recovery();
    test_bottleneck_explanation();
    test_thermal_and_stale();
    test_fps();
    test_contradictory_and_missing();
    test_corrupt_input();
    test_history();
    test_determinism_and_read_only();
    return flux_test::report("observatory_analysis_test");
}
