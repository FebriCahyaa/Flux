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

// fluxd's session manager with its real participants, in begin order:
//   1. Game Runtime performance context (GameRuntimeHost)
//   2. SessionRecorder (statistics; sessions.json format unchanged)
//   3. RuntimeMetrics (read-only sampling)
//   4. Live policy (DecisionEngine + PolicyExecutor, Phase 4C)
// End runs in reverse: the policy transaction is restored first, then sampling stops, statistics
// are finished, and GameRuntime restores its per-game values last.

#include "SessionManager.hpp"

#include <Flux.hpp>

#include <functional>

namespace flux_session {

flux::session::SessionManager &manager();

/// Profile intent for live policy decisions (the daemon's current profile mode).
void set_profile_source(std::function<FluxProfileMode()> source);

} // namespace flux_session
