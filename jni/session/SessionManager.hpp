/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// Session lifecycle (docs/architecture/SESSION_MODEL.md).
//
// Main.cpp reports game events; the SessionManager owns the ordering of what happens on them.
// Participants (GameRuntime performance context, SessionRecorder, ...) keep owning their own work.
//
//   begin : participants in registration order      (GameRuntime first, then SessionRecorder)
//   end   : participants in reverse order            (SessionRecorder first, then GameRuntime restore)
//
// Exactly one session is active. begin() is idempotent for the same package + pid; another game or
// a restarted process ends the current session (Switch) before the new one begins.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace flux::session {

enum class EndReason { Exit, FocusLost, ProcessDeath, Switch, Failure, DaemonStop };
const char *to_string(EndReason r);

struct SessionKey {
    std::string package;
    int pid = 0;              ///< tracked game process
    int uid = 0;
    std::vector<int> pids;    ///< every process of the game the observers should watch
    bool same_process(const SessionKey &o) const { return package == o.package && pid == o.pid; }
};

struct SessionInfo {
    std::string id;           ///< "s-<ms>-<seq>", unique per daemon run
    SessionKey key;
    int64_t started_ms = 0;
    int64_t ended_ms = 0;
    bool active = false;
    EndReason end_reason = EndReason::Exit;
};

/// One component with work tied to a session. The manager decides when; the participant decides how.
class SessionParticipant {
public:
    virtual ~SessionParticipant() = default;
    virtual const char *name() const = 0;
    /// Daemon start, before any session: undo what a previous daemon left behind.
    virtual void recover() {}
    virtual void begin(const SessionInfo &session) = 0;
    /// False when the participant could not clean up completely (logged, never blocks the others).
    virtual bool end(const SessionInfo &session, EndReason why) = 0;
    /// The Flux profile script ran again during the session.
    virtual void profile_applied(const SessionInfo &) {}
    virtual void tick(const SessionInfo &, int64_t /*now_ms*/) {}
    virtual bool needs_tick() const { return false; }
};

/// What the manager reports to an observer, always after all participants finished the transition.
struct SessionNotice {
    enum class Kind { Start, End, Switch } kind = Kind::Start;
    SessionInfo session;
    std::string previous_id; ///< Switch: the session that was replaced
    bool clean = true;       ///< End: every participant cleaned up
};

class SessionManager {
public:
    /// Optional; exceptions are swallowed and never change the lifecycle.
    void set_observer(std::function<void(const SessionNotice &)> o) { observer_ = std::move(o); }
    /// Optional; receives the session id before participants begin and "" after they ended, so
    /// what participants report can carry the id.
    void set_context(std::function<void(const std::string &session_id)> c) { context_ = std::move(c); }

    /// Registration order is begin order; end runs in reverse.
    void add(SessionParticipant *participant) { participants_.push_back(participant); }

    /// Daemon start. Call once, before the first begin().
    void recover();

    /// Returns true when a new session began with this call.
    bool begin(const SessionKey &key, int64_t now_ms);
    /// Returns false when nothing was active (duplicate end) or a participant did not clean up.
    bool end(EndReason why, int64_t now_ms);

    void profile_applied();
    void tick(int64_t now_ms);
    bool needs_tick() const;

    bool active() const { return current_.active; }
    const SessionInfo &current() const { return current_; }
    /// The last session that ended (for logs and later telemetry); empty id when none yet.
    const SessionInfo &last() const { return last_; }
    const std::vector<std::string> &events() const { return events_; }

private:
    std::vector<SessionParticipant *> participants_;
    SessionInfo current_, last_;
    uint64_t seq_ = 0;
    bool recovered_ = false;
    std::vector<std::string> events_; ///< bounded lifecycle log for diagnostics
    void note(const std::string &e);
    void notify(const SessionNotice &n) const;
    void context(const std::string &id) const;
    std::function<void(const SessionNotice &)> observer_;
    std::function<void(const std::string &)> context_;
};

} // namespace flux::session
