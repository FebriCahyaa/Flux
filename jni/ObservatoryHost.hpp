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

// fluxd's Observatory: an in-memory, bounded event store fed by the integration bridge.
// Nothing is persisted (device telemetry storage and retention come later).

#include "EventStore.hpp"
#include "TelemetryStore.hpp"
#include "ObservatoryBridge.hpp"

namespace flux_observatory {

flux::observatory::MemoryEventStore &store();
/// Persistent telemetry under /data/adb/.config/zairenkai (Step 8.11).
flux::observatory::PersistentEventStore &persistent();
/// What producers write to: memory first, then disk (failures isolated).
flux::observatory::EventSink &sink();
flux::bridge::ObservatoryBridge &bridge();

/// Daemon start: open the telemetry store, load/create the installation epoch, run retention.
/// Never throws; problems are logged and leave the memory store working.
void start();
/// Retention when due (hourly); call from the main loop.
void maintain_if_due();
/// Poll timeout while no session ticks, so retention also runs on an idle daemon.
int idle_timeout_ms();

} // namespace flux_observatory
