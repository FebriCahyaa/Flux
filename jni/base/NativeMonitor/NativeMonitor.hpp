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

#include <string>

/**
 * @brief Native replacement for the SynthesisCore Java daemon.
 *
 * Talks to system services directly over libbinder_ndk (approach and
 * callback handling adopted from Encore Tweaks' BinderMonitor):
 *  - IProcessObserver callbacks        -> focused app (package, pid, uid)
 *  - IDisplayManagerCallback callbacks -> screen state
 *  - periodic binder queries           -> battery saver, zen mode, thermal
 *    headroom/status, audio playback and call mode
 *  - power_supply sysfs                -> charging, battery level/temperature
 *
 * It writes SYNTHESIS_CORE_FILE in the SynthesisCore protocol, so fluxd's
 * inotify pipeline and the WebUI work unchanged. Transaction codes come from
 * BINDER_CODES_FILE, resolved once per boot by `synthesiscore.apk --resolve`.
 */
namespace NativeMonitor {

/**
 * @brief Starts monitoring.
 *
 * @return true when all required transaction codes and services were found and
 *         the observers are registered; false to fall back to the Java daemon.
 */
bool start();

/** @brief Human-readable reason for the last start() failure. */
std::string last_error();

} // namespace NativeMonitor
