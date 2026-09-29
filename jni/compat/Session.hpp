#pragma once

// SessionRuntime: the daemon's view of the Game Runtime.
//
// Main.cpp already owns "which game is this and is it still running" (SynthesisCore
// focus, PIDTracker, the 3-strike focus check). It calls this class at the points where
// that lifecycle starts and ends a game session; nothing here detects games or tracks
// processes. What this class adds:
//
//   - idempotent begin()/end(): the daemon re-evaluates the profile on every wake, so
//     begin() is called many times per session and must activate exactly once;
//   - one session at a time: begin() for another game (or a restarted process) restores
//     the previous one first;
//   - a persisted journal, so a crash or reboot mid-session is undone by recover();
//   - compatibility failure never blocks performance: the caller keeps applying the Flux
//     profile whatever begin() reports.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Analyze.hpp"
#include "GameRuntime.hpp"

namespace flux::compat {

struct SessionKey {
    std::string package;
    int pid = 0;
    int uid = 0;
    bool operator==(const SessionKey &o) const { return package == o.package && pid == o.pid; }
};

enum class EndReason { Exit, ProcessDeath, FocusLost, Switch, Failure, DaemonStop };
const char *to_string(EndReason r);

struct RecoveryReport {
    bool journal_found = false;
    size_t found = 0, restored = 0;
    size_t failed = 0;
    std::string package;
};

struct SessionDeps {
    RuntimeDeps runtime;
    std::string journal_path;   ///< persisted rollback journal
    std::string status_path;    ///< process-context snapshot for the WebUI (optional)
    /// Loads the documents and resolves the profile; the daemon reads them from disk.
    std::function<bool(const SessionKey &, ResolvedInputs &, std::string &error)> inputs;
    /// Refresh request for the profile script (0 = none). Set before the Flux profile is
    /// applied, cleared when the session ends.
    std::function<void(int hz)> set_refresh_request;
    /// Log sinks (level semantics follow the daemon: info = lifecycle, warn = degraded, debug = detail).
    std::function<void(const std::string &)> info, warn, debug;
};

class SessionRuntime {
public:
    explicit SessionRuntime(SessionDeps deps) : d_(std::move(deps)), rt_(d_.runtime) {}

    /// Replay a journal left by a crashed or killed daemon. Call before anything else
    /// touches the device. The journal is deleted only when every line verified.
    RecoveryReport recover();

    /// Start (or continue) the session for @p key. Returns true only when a new session
    /// was started by this call; a repeat for the same key returns false and does nothing.
    bool begin(const SessionKey &key);
    /// Per-game overrides, after Flux applied its own profile. First call applies them,
    /// later calls re-assert them (the profile script may have rewritten the nodes).
    void after_profile_applied();
    /// Start the per-game overrides if this session has not yet, without re-asserting them.
    /// For paths where Flux decided its profile was already in place and did not re-run it.
    void ensure_perf_started() {
        if (active_ && !perf_started_) after_profile_applied();
    }
    /// End the session and restore. Idempotent: returns false when nothing was active.
    bool end(EndReason why);

    bool active() const { return active_; }
    const SessionKey &key() const { return key_; }
    const Activation &last() const { return last_; }
    ContextState context() const { return last_.context; }

private:
    void persist_journal();
    void persist_status();
    void say(const std::function<void(const std::string &)> &sink, const std::string &m) const {
        if (sink) sink(m);
    }

    SessionDeps d_;
    GameRuntime rt_;
    bool active_ = false;
    bool perf_started_ = false;
    SessionKey key_;
    Activation last_;
    ContextState status_ = ContextState::Inactive;
    std::vector<std::string> carry_; ///< journal lines a recovery could not restore; never dropped
};

} // namespace flux::compat
