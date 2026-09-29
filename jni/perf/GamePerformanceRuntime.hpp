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

// Game Runtime performance owner (docs/architecture/GAME_RUNTIME_PERFORMANCE_LIFECYCLE.md).
//
// ProfileModel (what the game asks for) -> PerformancePlanner (what to change) -> Transaction
// Engine (how to change it safely). The caller reports game start/exit; this class never detects
// games, tracks processes or models sessions.

#include "PerformancePlanner.hpp"
#include "ProfileModel.hpp"
#include "Transaction.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flux::perf {

/// Whole-file access for profiles and journals (the node Io is separate).
struct FileStore {
    std::function<std::optional<std::string>(const std::string &path)> read;
    std::function<bool(const std::string &path, const std::string &text)> write_atomic;
    std::function<bool(const std::string &path)> remove;
};

struct RuntimePaths {
    std::string game_profiles;  ///< game_profiles.json
    std::string library;        ///< compat_library.json (presets only)
    std::string perf_journal;
    std::string launch_journal;
    std::string legacy_journal; ///< compat_journal from the old branch; recovered, never written
};

struct RuntimeDeps {
    flux::runtime::Io node_io;
    FileStore files;
    RuntimePaths paths;
    PerfCapabilities caps;
    std::function<bool(const std::string &category)> mitigation_allows;
    /// gamelist.json lite_mode for a package; nullopt when the game is not listed.
    std::function<std::optional<bool>(const std::string &package)> gamelist_lite;
    std::function<void(const std::string &)> log;
};

enum class RuntimeState { Idle, ResolveFailed, Failed, Active };
enum class EndReason { Exit, ProcessDeath, Switch, Failure, DaemonStop };
const char *to_string(RuntimeState s);
const char *to_string(EndReason r);

struct RecoveryResult {
    std::string journal;
    flux::runtime::RecoveryReport report;
    bool removed = false; ///< journal deleted because recovery was clean
};

class GamePerformanceRuntime {
public:
    explicit GamePerformanceRuntime(RuntimeDeps deps);

    /// Daemon start: replay every journal left behind. Call before any profile is applied.
    std::vector<RecoveryResult> recover();

    /// Start (or keep) the game's performance context. Idempotent for the same package + pid;
    /// a different game or a restarted process ends the previous context first.
    /// Returns true when a new context was started by this call (applied or not).
    bool on_game_start(const std::string &package, int pid, int64_t now_ms);

    /// Drive the launch-boost deadline from the caller's loop.
    void tick(int64_t now_ms);

    /// The Flux profile script ran again and may have overwritten per-game values.
    bool after_profile_script();

    /// Restore everything. Idempotent; true when nothing is left behind.
    bool on_game_end(EndReason why);

    RuntimeState state() const { return state_; }
    const std::string &package() const { return package_; }
    int pid() const { return pid_; }
    int refresh_target_hz() const { return refresh_target_hz_; }
    bool launch_boost_active() const { return boost_ && boost_->phase() == LaunchBoost::Phase::Boosting; }
    const ResolvedProfile &profile() const { return profile_; }
    const std::vector<std::string> &warnings() const { return warnings_; }
    const std::string &last_error() const { return error_; }

private:
    void log(const std::string &m) const {
        if (d_.log) d_.log(m);
    }
    std::string next_tx_id(int64_t now_ms);
    flux::runtime::Transaction::JournalSink sink_for(const std::string &path);
    bool load_profiles(ProfileDocument &doc);

    RuntimeDeps d_;
    PerformancePlanner planner_;
    std::unique_ptr<flux::runtime::Transaction> tx_;
    std::unique_ptr<LaunchBoost> boost_;
    RuntimeState state_ = RuntimeState::Idle;
    std::string package_;
    int pid_ = 0;
    int refresh_target_hz_ = 0;
    uint64_t seq_ = 0;
    ResolvedProfile profile_;
    std::vector<std::string> warnings_;
    std::string error_;
};

} // namespace flux::perf
