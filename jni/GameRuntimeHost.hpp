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

// Daemon-side binding of the Game Runtime (jni/compat/Session.hpp) to the real device.
//
// Main.cpp remains the only place that decides a game started or ended; it calls the
// functions below at those points. Everything stateful lives in SessionRuntime, which
// is covered by tests/session_test.cpp — this file only supplies the real filesystem,
// the on-disk documents, the device mitigation gate and the log sinks.

#include <string>
#include <sys/types.h>

#include "Session.hpp"

namespace flux_runtime {

/// Replay a journal left by a crashed daemon. Call once at startup, before any profile is applied.
void recover_at_boot();

/// A game session for @p package (tracked process @p pid) is running. Safe to call on every
/// daemon wake: only the first call for a session does anything.
void begin(const std::string &package, pid_t pid, uid_t uid);

/// The Flux performance profile has just been (re)applied for the active game.
void after_profile_applied();

/// Start the per-game overrides for a session that has none yet; a no-op otherwise. Covers the
/// paths where the Flux profile was judged already in place and not re-run.
void ensure_perf_started();

/// The session ended (exit, process death, focus loss, ...). Idempotent.
void end(flux::compat::EndReason reason);

/// Refresh rate (Hz) the active game asked for, or 0. Read by set_profiler_env_vars().
int refresh_request();

} // namespace flux_runtime
