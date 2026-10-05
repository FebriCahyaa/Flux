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

// fluxd's runtime capability context (Step 7.6): one context per daemon, filled by the Kernel
// Intelligence probe at start. Engines read it; only the bootstrap publishes. Read-only probing.

#include "CapabilityBootstrap.hpp"
#include "CapabilityContext.hpp"
#include "CapabilityVerification.hpp"

#include <memory>

namespace flux_capability {

/// Read-only view for engines. Empty (everything Unknown) until bootstrap() ran.
std::shared_ptr<const flux::context::CapabilityContext> context();

/// Probe the kernel and publish. Never throws; a failure is logged and leaves capabilities Unknown.
const flux::kernel::BootstrapResult &bootstrap();

/// Capability verification (Step 8.14): once per daemon start, after GameRuntime recovery and
/// before profile scripts. Replays a leftover verification journal first; skipped while Synrei
/// actively manages thermal state. Marks verified only after write/read-back/restore/read-back.
/// Never throws; failures only log.
void verify();

} // namespace flux_capability
