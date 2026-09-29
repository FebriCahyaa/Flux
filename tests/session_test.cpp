// Lifecycle tests for the daemon-facing SessionRuntime (jni/compat/Session.*).
// The daemon's game detection is not under test here; these tests drive begin()/end()
// the way Main.cpp does, including the repeated calls its wake-per-event loop makes.

#include <map>
#include <set>

#include "flux_test.hpp"

#include "Session.hpp"

using namespace flux::compat;

namespace {

struct FakeFs {
    std::map<std::string, std::string> files;
    std::set<std::string> fail_writes;
    std::vector<std::string> writes;

    Io io() {
        Io i;
        i.exists = [this](const std::string &p) { return files.count(p) > 0; };
        i.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = files.find(p);
            if (it == files.end()) return std::nullopt;
            return it->second + "\n";
        };
        i.write = [this](const std::string &p, const std::string &v) {
            if (fail_writes.count(p)) return false;
            writes.push_back(p);
            files[p] = v;
            return true;
        };
        return i;
    }
};

constexpr const char *kJournal = "/cfg/compat_journal";
constexpr const char *kStatus = "/cfg/compat_status.json";

RealHardware hw120() {
    RealHardware hw;
    hw.soc = "soc";
    hw.vulkan = Tri::Yes;
    hw.refresh_peak_hz = 120;
    hw.refresh_modes_hz = {60, 90, 120};
    hw.sdk = 34;
    return hw;
}

void seed(FakeFs &fs) {
    fs.files["/proc/sys/vm/swappiness"] = "100";
    fs.files["/proc/sys/vm/vfs_cache_pressure"] = "120";
    fs.files["/proc/sys/vm/page-cluster"] = "3";
    fs.files["/sys/block/sda/queue/read_ahead_kb"] = "128";
    fs.files["/sys/block/sda/queue/rq_affinity"] = "1";
}

struct Harness {
    FakeFs fs;
    NativeBackend native;
    ProfileLibrary lib;
    std::map<std::string, EffectiveProfile> profiles; // by package
    RealHardware hw = hw120();
    bool inputs_fail = false;
    std::vector<int> refresh_requests;
    std::vector<std::string> info, warn;
    std::unique_ptr<SessionRuntime> rt;
    Backend *zygisk = nullptr;

    Harness() {
        std::string err;
        lib.load_json(R"({"identities":{"fx":{"layer":"device","fields":{"MODEL":"X"}}}})", err);
        seed(fs);
    }

    void build() {
        SessionDeps d;
        d.runtime.io = fs.io();
        d.runtime.native = &native;
        d.runtime.zygisk = zygisk;
        d.runtime.library = &lib;
        d.runtime.block_queues = {"/sys/block/sda/queue"};
        d.journal_path = kJournal;
        d.status_path = kStatus;
        d.inputs = [this](const SessionKey &k, ResolvedInputs &out, std::string &err) {
            if (inputs_fail) { err = "documents unreadable"; return false; }
            out.profile = profiles.count(k.package) ? profiles[k.package] : EffectiveProfile{};
            out.profile.package = k.package;
            out.hw = hw;
            out.lib = lib;
            out.has_profile = profiles.count(k.package) > 0;
            return true;
        };
        d.set_refresh_request = [this](int hz) { refresh_requests.push_back(hz); };
        d.info = [this](const std::string &m) { info.push_back(m); };
        d.warn = [this](const std::string &m) { warn.push_back(m); };
        rt = std::make_unique<SessionRuntime>(d);
    }

    EffectiveProfile &game(const std::string &pkg) {
        EffectiveProfile &e = profiles[pkg];
        e.package = pkg;
        return e;
    }

    bool logged(const std::string &needle) const {
        for (const auto &l : info) if (l.find(needle) != std::string::npos) return true;
        for (const auto &l : warn) if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};

std::string journal_of(Harness &h) {
    auto it = h.fs.files.find(kJournal);
    return it == h.fs.files.end() ? "" : it->second;
}

// -- activate / deactivate -----------------------------------------------------

void test_activate_once_and_duplicate() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    SessionKey k{"a.game", 100, 10100};

    CHECK(h.rt->begin(k));
    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");
    size_t writes_after_first = h.fs.writes.size();

    // The daemon calls this on every wake for the whole session.
    for (int i = 0; i < 5; ++i) CHECK(!h.rt->begin(k));
    CHECK(h.fs.writes.size() == writes_after_first);

    // Snapshot must still be the pre-game value after many repeats.
    h.rt->end(EndReason::Exit);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
}

void test_deactivate_once_and_duplicate() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();

    CHECK(h.rt->end(EndReason::Exit));
    CHECK(!h.rt->active());
    size_t n = h.fs.writes.size();
    // Someone else changes the node after the game; a second end() must not undo it.
    h.fs.files["/proc/sys/vm/swappiness"] = "77";
    CHECK(!h.rt->end(EndReason::ProcessDeath));
    CHECK(!h.rt->end(EndReason::FocusLost));
    CHECK(h.fs.writes.size() == n);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "77");
}

void test_end_without_begin_is_noop() {
    Harness h;
    h.build();
    CHECK(!h.rt->end(EndReason::Exit));
    CHECK(h.fs.writes.empty());
}

void test_game_switch_restores_first() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.game("b.game").storage = "gaming";
    h.build();

    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");

    h.fs.writes.clear();
    CHECK(h.rt->begin({"b.game", 2, 2})); // no end() in between: the switch does it
    // A's state is gone before B's first write.
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/sys/block/sda/queue/read_ahead_kb"], "512");
    CHECK_EQ(h.rt->key().package, "b.game");
    CHECK(h.logged("reason=switch"));

    // A's nodes were restored strictly before any of B's were written.
    size_t first_b = h.fs.writes.size(), last_a_restore = 0;
    for (size_t i = 0; i < h.fs.writes.size(); ++i) {
        if (h.fs.writes[i].find("read_ahead") != std::string::npos && first_b == h.fs.writes.size()) first_b = i;
        if (h.fs.writes[i] == "/proc/sys/vm/swappiness") last_a_restore = i;
    }
    CHECK(last_a_restore < first_b);

    h.rt->end(EndReason::Exit);
    CHECK_EQ(h.fs.files["/sys/block/sda/queue/read_ahead_kb"], "128");
}

void test_process_restart_is_a_new_session() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 10, 1});
    h.rt->after_profile_applied();
    CHECK(h.rt->begin({"a.game", 11, 1})); // same package, new PID
    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");
    h.rt->end(EndReason::ProcessDeath);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100"); // not "60" captured from the first run
}

void test_process_death_restores() {
    Harness h;
    h.game("a.game").memory = "gaming_plus";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    CHECK(h.rt->end(EndReason::ProcessDeath));
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
    CHECK_EQ(h.fs.files["/proc/sys/vm/vfs_cache_pressure"], "120");
    CHECK(h.logged("reason=process_death"));
    CHECK(h.logged("restore=PASS"));
}

// -- failure isolation ---------------------------------------------------------------

void test_backend_unavailable_keeps_performance() {
    Harness h;
    auto &e = h.game("a.game");
    e.mode = Mode::Advanced;
    e.device_profile = "fx"; // needs a process-scoped backend, and there is none
    e.memory = "gaming";
    h.build();

    CHECK(h.rt->begin({"a.game", 1, 1}));
    CHECK(h.rt->context() == ContextState::Failed);
    CHECK(h.rt->last().backend_state == BackendState::Unavailable);
    CHECK(h.logged("compatibility=FAILED"));
    CHECK(h.logged("performance_fallback=CONTINUE"));

    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60"); // performance side unaffected

    // Nothing system-wide was written for the failed identity.
    for (const auto &w : h.fs.writes) CHECK(w.find("compat") == std::string::npos || w == kStatus || w == kJournal);
    h.rt->end(EndReason::Exit);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
}

void test_input_failure_falls_back() {
    Harness h;
    h.inputs_fail = true;
    h.build();
    CHECK(h.rt->begin({"a.game", 1, 1}));
    CHECK(h.logged("performance_fallback=CONTINUE"));
    CHECK(!h.rt->begin({"a.game", 1, 1})); // not retried on every wake
    h.rt->after_profile_applied();          // harmless
    CHECK(h.fs.writes.size() <= 2);          // at most status writes
    CHECK(h.rt->end(EndReason::Exit));
}

void test_perf_apply_failure_rolls_back() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.fs.fail_writes.insert("/proc/sys/vm/page-cluster");
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    CHECK(h.rt->last().perf_context == ContextState::Failed);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100"); // rolled back
    CHECK(h.logged("FAILED (rolled back)"));
    CHECK(h.rt->end(EndReason::Exit));
}

void test_restore_failure_keeps_journal() {
    Harness h;
    h.game("a.game").memory = "balanced";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    CHECK(!journal_of(h).empty());
    h.fs.fail_writes.insert("/proc/sys/vm/vfs_cache_pressure");
    CHECK(h.rt->end(EndReason::Exit));
    CHECK(h.logged("restore=FAILED"));
    CHECK(!journal_of(h).empty()); // left for the next boot
}

// -- no-op paths ---------------------------------------------------------------------------

void test_unprofiled_game_changes_nothing() {
    Harness h; // no profile entry: EffectiveProfile defaults are Real / default
    h.build();
    h.rt->begin({"plain.game", 1, 1});
    h.rt->after_profile_applied();
    for (const auto &w : h.fs.writes) CHECK(w == kStatus || w == kJournal);
    CHECK(h.refresh_requests.size() == 1 && h.refresh_requests[0] == 0);
    h.rt->end(EndReason::Exit);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
}

void test_ensure_perf_started_covers_skipped_profile() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    // Flux judged its profile already applied and did not call after_profile_applied().
    h.rt->ensure_perf_started();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");
    size_t n = h.fs.writes.size();
    h.rt->ensure_perf_started(); // idempotent: not a re-assert
    h.rt->ensure_perf_started();
    CHECK(h.fs.writes.size() == n);
    h.rt->end(EndReason::Exit);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
}

// -- re-assert ---------------------------------------------------------------------------------

void test_reassert_after_profile_rewrite() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    h.fs.files["/proc/sys/vm/swappiness"] = "30"; // thermal switch: the script wrote its own value
    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");
    h.rt->end(EndReason::Exit);
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100"); // original snapshot, not the script's 30
}

// -- refresh --------------------------------------------------------------------------------------

void test_refresh_request_and_restore() {
    Harness h;
    h.game("a.game").refresh = "hz120";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    CHECK(h.refresh_requests.back() == 120);
    h.rt->end(EndReason::Exit);
    CHECK(h.refresh_requests.back() == 0); // cleared so the profile script restores the user's setting
}

void test_refresh_not_requested_beyond_panel() {
    Harness h;
    h.hw.refresh_peak_hz = 60;
    h.hw.refresh_modes_hz = {60};
    h.game("a.game").refresh = "hz120";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    CHECK(h.refresh_requests.back() == 0);
    CHECK(h.logged("refresh not requested"));
    h.rt->end(EndReason::Exit);
}

void test_refresh_adaptive_and_real_request_nothing() {
    for (const char *mode : {"adaptive", "real"}) {
        Harness h;
        h.game("a.game").refresh = mode;
        h.build();
        h.rt->begin({"a.game", 1, 1});
        CHECK(h.refresh_requests.back() == 0);
        h.rt->end(EndReason::Exit);
    }
}

// -- journal / recovery -----------------------------------------------------------------------------

void test_journal_written_during_session_and_cleared() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    CHECK(journal_of(h).empty()); // compat-only phase: nothing to undo yet
    h.rt->after_profile_applied();
    std::string j = journal_of(h);
    CHECK(j.find("#package=a.game") != std::string::npos);
    CHECK(j.find("/proc/sys/vm/swappiness\t100") != std::string::npos);
    h.rt->end(EndReason::Exit);
    CHECK(journal_of(h).empty());
}

void test_recovery_after_daemon_crash() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.game("a.game").storage = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");

    // Daemon dies mid-game: the runtime object is gone, the files stay.
    h.rt.reset();
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "60");

    h.build(); // fresh daemon
    auto rep = h.rt->recover();
    CHECK(rep.journal_found);
    CHECK(rep.failed == 0);
    CHECK_EQ(rep.package, "a.game");
    CHECK_EQ(h.fs.files["/proc/sys/vm/swappiness"], "100");
    CHECK_EQ(h.fs.files["/sys/block/sda/queue/read_ahead_kb"], "128");
    CHECK_EQ(h.fs.files["/sys/block/sda/queue/rq_affinity"], "1");
    CHECK(journal_of(h).empty());
    CHECK(h.logged("recovery=PASS"));

    // Idempotent: a second recovery finds nothing and writes nothing.
    size_t n = h.fs.writes.size();
    auto again = h.rt->recover();
    CHECK(!again.journal_found);
    CHECK(h.fs.writes.size() == n);
}

void test_recovery_with_clean_shutdown_does_nothing() {
    Harness h;
    h.build();
    auto rep = h.rt->recover();
    CHECK(!rep.journal_found);
    CHECK(h.fs.writes.empty());
}

void test_recovery_failure_keeps_only_failed_lines() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    h.rt.reset();

    h.fs.fail_writes.insert("/proc/sys/vm/swappiness");
    h.build();
    auto rep = h.rt->recover();
    CHECK(rep.failed == 1);
    CHECK(rep.restored + 1 == rep.found);
    CHECK_EQ(h.fs.files["/proc/sys/vm/vfs_cache_pressure"], "120"); // the others did restore
    std::string j = journal_of(h);
    CHECK(j.find("swappiness") != std::string::npos);
    CHECK(j.find("vfs_cache_pressure") == std::string::npos);
    CHECK(h.logged("recovery=FAILED"));

    // A later session must not erase what is still unrecovered.
    h.fs.fail_writes.clear();
    h.game("b.game").storage = "gaming";
    h.rt->begin({"b.game", 2, 2});
    h.rt->after_profile_applied();
    CHECK(journal_of(h).find("swappiness") != std::string::npos);
    h.rt->end(EndReason::Exit);
    CHECK(journal_of(h).find("swappiness") != std::string::npos);
}

void test_recovery_ignores_hostile_journal() {
    Harness h;
    h.fs.files[kJournal] = "#flux-compat-journal v1\n#package=x\n../../etc/passwd\tboom\nrelative\tboom\nnotab\n";
    h.build();
    auto rep = h.rt->recover();
    CHECK(rep.journal_found);
    CHECK(rep.failed == 3);
    for (const auto &w : h.fs.writes) CHECK(w == kJournal);
}

void test_recovery_verifies_by_reading_back() {
    // The write "succeeds" but the kernel clamps the value: recovery must not claim PASS.
    Harness h;
    h.fs.files["/proc/sys/vm/swappiness"] = "60";
    h.fs.files[kJournal] = "#flux-compat-journal v1\n#package=x\n/proc/sys/vm/swappiness\t100\n";
    h.build();
    auto io = h.fs.io();
    io.write = [&h](const std::string &p, const std::string &v) {
        h.fs.writes.push_back(p);
        h.fs.files[p] = (p == "/proc/sys/vm/swappiness") ? "60" : v;
        return true;
    };
    SessionDeps d;
    d.runtime.io = io;
    d.runtime.native = &h.native;
    d.journal_path = kJournal;
    d.warn = [&h](const std::string &m) { h.warn.push_back(m); };
    SessionRuntime rt(d);
    auto rep = rt.recover();
    CHECK(rep.failed == 1);
    CHECK(!journal_of(h).empty());
}

// -- status file ---------------------------------------------------------------------------------------

void test_status_file_reports_state() {
    Harness h;
    h.game("a.game").memory = "gaming";
    h.build();
    h.rt->begin({"a.game", 1, 1});
    h.rt->after_profile_applied();
    std::string s = h.fs.files[kStatus];
    CHECK(s.find("\"active\":true") != std::string::npos);
    CHECK(s.find("\"package\":\"a.game\"") != std::string::npos);
    CHECK(s.find("\"perf_context\":\"active\"") != std::string::npos);
    CHECK(s.find("\"sustained\":\"unknown\"") != std::string::npos);
    h.rt->end(EndReason::Exit);
    s = h.fs.files[kStatus];
    CHECK(s.find("\"active\":false") != std::string::npos);
    CHECK(s.find("\"context\":\"restored\"") != std::string::npos);
}

void test_build_inputs_daemon_default_is_real() {
    AnalyzeInputs in;
    ResolvedInputs out;
    std::string err;
    CHECK(build_inputs("new.game", std::nullopt, Mode::Real, in, out, err));
    CHECK(out.profile.mode == Mode::Real);
    CHECK(!out.has_profile);
    in.profiles_json = R"({"p.game":{"performance":{"memory":"gaming"},"compatibility":{"mode":"auto"}}})";
    CHECK(build_inputs("p.game", std::nullopt, Mode::Real, in, out, err));
    CHECK(out.profile.mode == Mode::Auto);
    CHECK_EQ(out.profile.memory, "gaming");
    CHECK(!build_inputs("../x", std::nullopt, Mode::Real, in, out, err));
}

} // namespace

int main() {
    test_activate_once_and_duplicate();
    test_deactivate_once_and_duplicate();
    test_end_without_begin_is_noop();
    test_game_switch_restores_first();
    test_process_restart_is_a_new_session();
    test_process_death_restores();
    test_backend_unavailable_keeps_performance();
    test_input_failure_falls_back();
    test_perf_apply_failure_rolls_back();
    test_restore_failure_keeps_journal();
    test_unprofiled_game_changes_nothing();
    test_ensure_perf_started_covers_skipped_profile();
    test_reassert_after_profile_rewrite();
    test_refresh_request_and_restore();
    test_refresh_not_requested_beyond_panel();
    test_refresh_adaptive_and_real_request_nothing();
    test_journal_written_during_session_and_cleared();
    test_recovery_after_daemon_crash();
    test_recovery_with_clean_shutdown_does_nothing();
    test_recovery_failure_keeps_only_failed_lines();
    test_recovery_ignores_hostile_journal();
    test_recovery_verifies_by_reading_back();
    test_status_file_reports_state();
    test_build_inputs_daemon_default_is_real();
    return flux_test::report("session_test");
}
