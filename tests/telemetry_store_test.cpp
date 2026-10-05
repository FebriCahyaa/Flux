// Observatory persistent storage + 7-day retention (Step 8.11). Real temp directories with the
// POSIX file layer; a failing wrapper for disk-failure cases.
#include "flux_test.hpp"
#include "InstallationEpoch.hpp"
#include "TelemetryStore.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace flux::observatory;

namespace {

constexpr int64_t kHour = 3600LL * 1000;
constexpr int64_t kDay = 24 * kHour;
int64_t wall = 1'760'000'000'000; // 2025-10-09
int64_t steady = 1'000'000;

TelemetryClock clock_() { return {[] { return wall; }, [] { return steady; }}; }

struct Root {
    fs::path path;
    explicit Root(const std::string &tag)
        : path(fs::temp_directory_path() / ("flux_tel_" + tag + "_" + std::to_string(::getpid()))) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Root() { fs::remove_all(path); }
    std::string str() const { return path.string(); }
    fs::path events() const { return path / "telemetry/events"; }
};

/// Delegates to POSIX; individual operations can be made to fail.
struct FlakyIo : FileIo {
    std::shared_ptr<FileIo> real = make_posix_io();
    bool fail_append = false, fail_atomic = false, fail_mkdirs = false;
    bool mkdirs(const std::string &d) override { return !fail_mkdirs && real->mkdirs(d); }
    bool exists(const std::string &p) const override { return real->exists(p); }
    std::optional<std::string> read_all(const std::string &p) const override { return real->read_all(p); }
    std::optional<char> last_byte(const std::string &p) const override { return real->last_byte(p); }
    std::optional<uint64_t> size(const std::string &p) const override { return real->size(p); }
    bool append(const std::string &p, const std::string &d) override { return !fail_append && real->append(p, d); }
    bool write_atomic(const std::string &p, const std::string &d) override { return !fail_atomic && real->write_atomic(p, d); }
    bool remove(const std::string &p) override { return real->remove(p); }
    std::vector<std::string> list(const std::string &d) const override { return real->list(d); }
};

int counter = 0;
Event ev(const std::string &type, int64_t ts, const std::string &session = "s-1",
         std::map<std::string, std::string> after = {}) {
    Event e;
    e.event_id = "ev-" + std::to_string(++counter);
    e.timestamp_ms = ts;
    e.type = type;
    e.session_id = session;
    e.reason = "test";
    e.after = std::move(after);
    if (type.rfind("SESSION", 0) == 0) e.source = "session";
    else if (type.rfind("TRANSACTION", 0) == 0) {
        e.source = "transaction";
        e.transaction_id = "tx-" + std::to_string(counter);
    } else if (type.rfind("BOTTLENECK", 0) == 0) e.source = "bottleneck";
    else if (type.rfind("RECOVERY", 0) == 0) {
        e.source = "recovery";
        e.session_id.clear();
    }
    return e;
}

std::string read_file(const fs::path &p) {
    std::ifstream in(p);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<fs::path> segments(const Root &r) {
    std::vector<fs::path> out;
    if (!fs::exists(r.events())) return out;
    for (auto &d : fs::directory_iterator(r.events()))
        if (d.path().extension() == ".jsonl") out.push_back(d.path());
    std::sort(out.begin(), out.end());
    return out;
}

void test_persistent_write() {
    Root r("write");
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    CHECK(s.open());
    CHECK(s.health().open && s.health().format_ok);
    CHECK_EQ(read_file(r.path / "telemetry/FORMAT"), std::string("zairenkai-telemetry 1\n"));
    auto a = ev("SESSION_START", wall - 10, "s-1", {{"package", "com.a"}});
    auto b = ev("BOTTLENECK_ASSESSED", wall - 5);
    CHECK(s.append(a));
    CHECK(s.append(b));
    CHECK_EQ(s.health().written, uint64_t(2));
    auto segs = segments(r);
    CHECK_EQ(segs.size(), size_t(1));
    // Persisted line is exactly the event contract (to_json) — nothing else is stored.
    CHECK_EQ(read_file(segs[0]), to_json(a) + "\n" + to_json(b) + "\n");
    auto q = s.query({});
    CHECK(q.size() == 2 && q[0].event_id == a.event_id && q[1].event_id == b.event_id);
    CHECK(fs::exists(segs[0].string().substr(0, segs[0].string().size() - 6) + ".idx"));
}

void test_restart_recovery_and_duplicates() {
    Root r("restart");
    auto a = ev("SESSION_START", wall - 100, "s-1", {{"package", "com.a"}});
    {
        PersistentEventStore s(r.str(), clock_(), make_posix_io());
        s.open();
        s.append(a);
        CHECK(!s.append(a)); // duplicate in the same run
        CHECK_EQ(s.health().duplicates, uint64_t(1));
    }
    PersistentEventStore s2(r.str(), clock_(), make_posix_io());
    CHECK(s2.open());
    CHECK_EQ(s2.query({}).size(), size_t(1));
    CHECK(!s2.append(a)); // replay after restart is still a duplicate
    // A duplicate line written outside the store is returned once.
    std::ofstream(segments(r)[0], std::ios::app) << to_json(a) << "\n";
    QueryStats st;
    CHECK_EQ(s2.query({}, &st).size(), size_t(1));
    CHECK_EQ(st.duplicates_skipped, uint64_t(1));
}

void test_corruption_isolation() {
    Root r("corrupt");
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    s.open();
    s.append(ev("SESSION_START", wall - 30));
    auto seg = segments(r)[0];
    std::ofstream(seg, std::ios::app) << "this is not json\n{\"schema\":1,\"event_id\":\"x\"}\n"
                                      << to_json(ev("SESSION_END", wall - 20)).substr(0, 40); // partial append, no newline
    // Restart during write: a new store appends after the torn line, on its own line.
    PersistentEventStore s2(r.str(), clock_(), make_posix_io());
    s2.open();
    CHECK(s2.append(ev("SESSION_SWITCH", wall - 10)));
    QueryStats st;
    auto q = s2.query({}, &st);
    CHECK_EQ(q.size(), size_t(2)); // both valid records recovered
    CHECK(st.corrupted_lines >= 3);
    CHECK(q.back().type == "SESSION_SWITCH");
    // Truncated file (cut in the middle of the last record) keeps the earlier ones.
    auto text = read_file(seg);
    std::ofstream(seg, std::ios::trunc) << text.substr(0, text.size() - 15);
    CHECK_EQ(s2.query({}).size(), size_t(1));
}

void test_retention() {
    Root r("retention");
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    s.open();
    // Written while they were fresh (wall moved forward later).
    const int64_t w0 = wall;
    wall = w0 - 10 * kDay;
    auto old = ev("SESSION_START", wall, "s-old");
    CHECK(s.append(old));
    wall = w0;
    auto boundary_keep = ev("SESSION_END", w0 - kRetentionMs, "s-b");      // exactly 7 x 24 h: kept
    auto boundary_drop = ev("SESSION_START", w0 - kRetentionMs - 1, "s-b"); // 1 ms older: removed
    auto fresh = ev("BOTTLENECK_ASSESSED", w0 - kHour, "s-f");
    // These timestamps are still inside the window at write time.
    wall = w0 - 2;
    CHECK(s.append(boundary_drop));
    wall = w0;
    CHECK(s.append(boundary_keep));
    CHECK(s.append(fresh));
    CHECK_EQ(s.query({}).size(), size_t(4));

    auto rep = s.maintain();
    CHECK(rep.ran && rep.skipped_reason.empty());
    CHECK_EQ(rep.cutoff_ms, w0 - kRetentionMs);
    CHECK_EQ(rep.segments_deleted, 1); // the 10-day-old hour
    CHECK(rep.records_removed >= 2);
    auto q = s.query({});
    CHECK_EQ(q.size(), size_t(2));
    for (auto &e : q) CHECK(e.timestamp_ms >= w0 - kRetentionMs);
    // Its index went with it; nothing but the two kept hours remain.
    size_t idx = 0;
    for (auto &d : fs::directory_iterator(r.events())) idx += d.path().extension() == ".idx";
    CHECK(idx <= segments(r).size());
    CHECK(!s.maintenance_due()); // just ran
    // Writing an already-expired record is refused, not stored.
    CHECK(!s.append(ev("SESSION_START", w0 - 8 * kDay)));
    CHECK_EQ(s.health().rejected, uint64_t(1));
    // Dry run reports without deleting.
    PersistentEventStore d(r.str(), clock_(), make_posix_io());
    d.open(true);
    wall = w0 + 6 * kDay + kHour + 1; // boundary_keep and fresh's hour... fresh still within
    auto dry = d.maintain(true);
    CHECK(dry.dry_run && dry.records_removed >= 1);
    CHECK_EQ(d.query({}).size(), size_t(2));
    wall = w0;
}

void test_clock_safety() {
    Root r("clock");
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    s.open();
    s.append(ev("SESSION_START", wall - kHour));
    const int64_t w0 = wall;
    // Unset clock (1970): retention does not run.
    wall = 5000;
    auto unset = s.maintain();
    CHECK(!unset.ran && unset.skipped_reason.find("not set") != std::string::npos);
    wall = w0;
    CHECK(s.maintain().ran);
    // Wall clock jumps 30 days ahead while only one hour passed on the steady clock: deferred.
    wall = w0 + 30 * kDay;
    steady += kHour;
    auto jump = s.maintain();
    CHECK(!jump.ran && jump.skipped_reason.find("jump") != std::string::npos);
    CHECK_EQ(s.query({}).size(), size_t(1)); // fresh data kept
    // Clock moved backwards: cutoff earlier, nothing extra removed.
    wall = w0 - 2 * kDay;
    steady += kHour;
    auto back = s.maintain();
    CHECK(back.ran && back.records_removed == 0);
    CHECK_EQ(s.query({}).size(), size_t(1));
    wall = w0;
}

void test_interrupted_cleanup() {
    Root r("interrupted");
    {
        PersistentEventStore s(r.str(), clock_(), make_posix_io());
        s.open();
        s.append(ev("SESSION_START", wall - kHour));
    }
    auto seg = segments(r)[0];
    auto idx = seg.string().substr(0, seg.string().size() - 6) + ".idx";
    // Leftovers of an interrupted trim / delete: temp file, orphan index, segment without index.
    std::ofstream(seg.string() + ".tmp") << "half";
    std::ofstream(r.events() / "20240101T00.idx") << "format=1\n";
    fs::remove(idx);
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    CHECK(s.open());
    CHECK(!fs::exists(seg.string() + ".tmp"));
    CHECK_EQ(s.query({}).size(), size_t(1)); // no index: segment scanned
    auto rep = s.maintain();
    CHECK(rep.orphans_removed >= 1);
    CHECK(!fs::exists(r.events() / "20240101T00.idx"));
}

void test_disk_write_failure() {
    Root r("fail");
    auto io = std::make_shared<FlakyIo>();
    PersistentEventStore disk(r.str(), clock_(), io);
    CHECK(disk.open());
    MemoryEventStore mem(EventRegistry::builtin(), [] { return wall; });
    TeeEventSink sink(mem, &disk, [] { return wall; });

    io->fail_append = true;
    auto w1 = sink.write(ev("SESSION_START", wall - 3));
    CHECK(w1.accepted); // the producer is unaffected: memory accepted it
    auto w2 = sink.write(ev("SESSION_END", wall - 2));
    CHECK(w2.accepted);
    CHECK(disk.health().failing && disk.health().failed == 2 && !disk.health().last_error.empty());
    EventQuery q;
    q.type = "OBSERVATORY_STORAGE_FAILED";
    CHECK_EQ(mem.query(q).size(), size_t(1)); // reported once per failure streak, memory only
    CHECK(disk.query({}).empty());               // no false success on disk
    io->fail_append = false;
    CHECK(sink.write(ev("SESSION_START", wall - 1)).accepted);
    CHECK(!disk.health().failing);
    CHECK_EQ(disk.query({}).size(), size_t(1));
    io->fail_append = true;
    sink.write(ev("SESSION_END", wall));
    CHECK_EQ(mem.query(q).size(), size_t(2)); // a new streak is reported again

    // Unusable directory: open fails, appends refuse, nothing throws.
    auto bad = std::make_shared<FlakyIo>();
    bad->fail_mkdirs = true;
    Root r2("fail2");
    PersistentEventStore d2(r2.str(), clock_(), bad);
    CHECK(!d2.open());
    CHECK(!d2.append(ev("SESSION_START", wall)));
    MemoryEventStore mem2(EventRegistry::builtin(), [] { return wall; });
    TeeEventSink sink2(mem2, &d2, [] { return wall; });
    CHECK(sink2.write(ev("SESSION_START", wall)).accepted);
    // Rejected by the memory store (invalid) -> never persisted.
    auto invalid = ev("SESSION_START", wall);
    invalid.reason.clear();
    CHECK(!sink.write(invalid).accepted);
    // Oversized record refused by the persistent store.
    io->fail_append = false;
    auto huge = ev("BOTTLENECK_ASSESSED", wall);
    for (int i = 0; i < 64; ++i) huge.after["k" + std::to_string(i)] = std::string(1000, 'x');
    CHECK(!disk.append(huge));
}

void test_index_query() {
    Root r("query");
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    s.open();
    const int64_t base = wall - 5 * kHour;
    s.append(ev("SESSION_START", base, "s-1", {{"package", "com.a"}}));
    auto tx = ev("TRANSACTION_APPLY", base + 10, "s-1");
    tx.severity = Severity::Warning;
    s.append(tx);
    s.append(ev("BOTTLENECK_ASSESSED", base + kHour, "s-1")); // no package key: found via session
    s.append(ev("SESSION_START", base + 2 * kHour, "s-2", {{"package", "com.b"}}));
    s.append(ev("RECOVERY_START", base + 3 * kHour, ""));
    s.append(ev("BOTTLENECK_ASSESSED", base + 3 * kHour + 5, "s-2"));

    TelemetryQuery bysession;
    bysession.session_id = "s-2";
    QueryStats st;
    auto r1 = s.query(bysession, &st);
    CHECK_EQ(r1.size(), size_t(2));
    CHECK(st.segments_skipped_by_index >= 2); // hours without s-2 are not read
    TelemetryQuery bypkg;
    bypkg.package = "com.a";
    auto r2 = s.query(bypkg);
    CHECK_EQ(r2.size(), size_t(3)); // start, transaction and the bottleneck result of s-1
    TelemetryQuery bytype;
    bytype.type = "BOTTLENECK_ASSESSED";
    CHECK_EQ(s.query(bytype).size(), size_t(2));
    TelemetryQuery bysrc;
    bysrc.source = "recovery";
    CHECK_EQ(s.query(bysrc).size(), size_t(1));
    TelemetryQuery bysev;
    bysev.min_severity = Severity::Warning;
    CHECK_EQ(s.query(bysev).size(), size_t(1));
    TelemetryQuery bytx;
    bytx.transaction_id = tx.transaction_id;
    CHECK_EQ(s.query(bytx).size(), size_t(1));
    TelemetryQuery bydate;
    bydate.from_ms = base + kHour;
    bydate.to_ms = base + 2 * kHour;
    CHECK_EQ(s.query(bydate).size(), size_t(2));
    TelemetryQuery lim;
    lim.limit = 2;
    auto l = s.query(lim);
    CHECK(l.size() == 2 && l[0].timestamp_ms == base);
}

void test_installation_epoch() {
    Root r("epoch");
    auto io = make_posix_io();
    EpochFacts f{"1.4.1", "14", "5.10.198-android12-9", "Xiaomi 2201116SG kona", "arm64-v8a"};
    int ids = 0;
    auto rnd = [&] { return std::string(31, 'a') + std::to_string(++ids % 10); };
    auto first = load_or_create_epoch(*io, r.str(), f, wall, rnd);
    CHECK(first.created && first.epoch && first.error.empty());
    CHECK_EQ(first.epoch->installation_id.size(), size_t(32));
    CHECK_EQ(first.epoch->installed_at_ms, wall);
    CHECK_EQ(first.epoch->first_version, std::string("1.4.1"));
    // Module update: new facts, same epoch.
    EpochFacts upd{"2.0.0", "15", "6.1.75-android14", "Xiaomi 2201116SG kona", "arm64-v8a"};
    auto again = load_or_create_epoch(*io, r.str(), upd, wall + kDay, rnd);
    CHECK(!again.created && again.epoch);
    CHECK_EQ(again.epoch->installation_id, first.epoch->installation_id);
    CHECK_EQ(again.epoch->first_version, std::string("1.4.1"));
    CHECK_EQ(again.epoch->installed_at_ms, wall);
    // Retention never touches it (outside telemetry/).
    PersistentEventStore s(r.str(), clock_(), io);
    s.open();
    wall += 30 * kDay;
    s.maintain();
    wall -= 30 * kDay;
    CHECK(fs::exists(r.path / "installation.json"));
    // Corrupt file: preserved and reported, never overwritten.
    std::ofstream(r.path / "installation.json", std::ios::trunc) << "{broken";
    auto bad = load_or_create_epoch(*io, r.str(), f, wall, rnd);
    CHECK(!bad.epoch && !bad.created && !bad.error.empty());
    CHECK_EQ(read_file(r.path / "installation.json"), std::string("{broken"));
    // Clock unset at first install: installed_at 0 (unknown), not a fake date.
    Root r2("epoch2");
    auto unset = load_or_create_epoch(*io, r2.str(), f, 1000, rnd);
    CHECK(unset.epoch && unset.epoch->installed_at_ms == 0);
    // Round trip.
    auto back = epoch_from_json(epoch_to_json(*first.epoch));
    CHECK(back && back->installation_id == first.epoch->installation_id && back->architecture == "arm64-v8a");
}

void test_read_only_open() {
    Root r("ro");
    PersistentEventStore s(r.str(), clock_(), make_posix_io());
    CHECK(!s.open(true)); // nothing there yet; read-only never creates
    CHECK(!fs::exists(r.path / "telemetry"));
    // Unknown future format: read-only, not written.
    fs::create_directories(r.path / "telemetry");
    std::ofstream(r.path / "telemetry/FORMAT") << "zairenkai-telemetry 9\n";
    PersistentEventStore f(r.str(), clock_(), make_posix_io());
    CHECK(!f.open());
    CHECK(!f.health().format_ok);
    CHECK(!f.append(ev("SESSION_START", wall)));
}

} // namespace

int main() {
    test_persistent_write();
    test_restart_recovery_and_duplicates();
    test_corruption_isolation();
    test_retention();
    test_clock_safety();
    test_interrupted_cleanup();
    test_disk_write_failure();
    test_index_query();
    test_installation_epoch();
    test_read_only_open();
    return flux_test::report("telemetry_store_test");
}
