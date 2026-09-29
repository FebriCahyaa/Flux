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

// fluxd's single Game Runtime performance host, wired to the real device (jni/perf/RuntimeHost.hpp).

#include <cstdint>

#include "RuntimeHost.hpp"

namespace flux_runtime {

/// Built on first use with the real adapters; the capability probe runs once, then.
flux::perf::RuntimeHost &host();

/// Refresh bridge for the profile script's environment (FLUX_REFRESH_TARGET_HZ); 0 = none.
int refresh_request();

int64_t now_ms();

} // namespace flux_runtime
