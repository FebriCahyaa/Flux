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

// Daemon-facing side of the Game Runtime performance lifecycle.
//
//   device adapters (real /sys, /proc, files)  ->  GamePerformanceRuntime  ->  RuntimeHost  <- Main.cpp
//
// RuntimeHost holds exactly the calls fluxd makes, so the daemon lifecycle is host-tested with
// fake adapters. It adds the refresh bridge (the target the profile script receives) and the
// global off switch; it detects nothing itself.

#include "GamePerformanceRuntime.hpp"

#include <memory>
#include <string>
#include <vector>

namespace flux::perf {

// -- device adapters -----------------------------------------------------------------------------

/// Node access on the real filesystem, optionally under @p root (host tests use a temp dir).
flux::runtime::Io make_node_io(const std::string &root = "");

/// Whole-file access: atomic replace (temp + fsync + rename, no symlink following).
FileStore make_file_store();

/// Read-only probe: readable nodes, internal block queues under <root>/sys/block, panel modes.
/// @param panel_rates  source of the panel's refresh modes (e.g. parsed `dumpsys display`).
PerfCapabilities probe_capabilities(const std::string &root, std::function<std::vector<int>()> panel_rates);

/// Whole-Hz refresh modes from `dumpsys display` text ("fps=119.99" -> 120), highest first, unique.
std::vector<int> parse_panel_rates(const std::string &dumpsys_display);

// -- host ----------------------------------------------------------------------------------------

class RuntimeHost {
public:
    explicit RuntimeHost(RuntimeDeps deps) : rt_(std::move(deps)) {}

    /// fluxd start, before any profile script runs: replay journals left by a previous instance.
    std::vector<RecoveryResult> on_daemon_start();

    /// Game in the foreground and screen on; called on every profile pass (idempotent).
    /// Must run before the profile script so the refresh bridge sees the request.
    void on_game_active(const std::string &package, int pid, int64_t now_ms);

    /// The profile script ran (performance / performance_lite) for the active game.
    void on_profile_applied();

    void on_game_end(EndReason why);
    void tick(int64_t now_ms);

    /// Refresh bridge: the rate for FLUX_REFRESH_TARGET_HZ, 0 when none. Only a rate the panel
    /// reports is ever returned (the planner guarantees it); disabled -> 0.
    int refresh_request_hz() const;

    /// disable_tweaks: ends any context and ignores game events until re-enabled.
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }
    /// True while something needs tick() (launch boost deadline).
    bool needs_tick() const { return rt_.launch_boost_active(); }

    const GamePerformanceRuntime &runtime() const { return rt_; }

private:
    GamePerformanceRuntime rt_;
    bool enabled_ = true;
};

} // namespace flux::perf
