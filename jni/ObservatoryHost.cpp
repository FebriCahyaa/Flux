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

#include "ObservatoryHost.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>

#include <sys/system_properties.h>
#include <sys/utsname.h>

#include "InstallationEpoch.hpp"
#include <FluxLog.hpp>

std::string get_module_version(); // FluxCLI.cpp

namespace flux_observatory {

namespace {
int64_t wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

flux::observatory::MemoryEventStore &store() {
    static flux::observatory::MemoryEventStore instance(flux::observatory::EventRegistry::builtin(), wall_ms, 1024);
    return instance;
}

namespace {
int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string property(const char *name) {
    char value[PROP_VALUE_MAX] = {};
    const int len = __system_property_get(name, value);
    return len > 0 ? std::string(value, static_cast<size_t>(len)) : std::string();
}

std::string random_hex32() {
    unsigned char b[16] = {};
    std::ifstream in("/dev/urandom", std::ios::binary);
    if (!in.read(reinterpret_cast<char *>(b), sizeof b)) return {};
    std::string out;
    char buf[3];
    for (unsigned char c : b) {
        std::snprintf(buf, sizeof buf, "%02x", c);
        out += buf;
    }
    return out;
}
} // namespace

flux::observatory::PersistentEventStore &persistent() {
    static flux::observatory::PersistentEventStore instance(flux::observatory::kTelemetryRoot, {wall_ms, steady_ms},
                                                           flux::observatory::make_posix_io());
    return instance;
}

flux::observatory::EventSink &sink() {
    static flux::observatory::TeeEventSink instance(store(), &persistent(), wall_ms);
    return instance;
}

flux::bridge::ObservatoryBridge &bridge() {
    static flux::bridge::ObservatoryBridge instance(&sink(), wall_ms);
    return instance;
}

void start() {
    try {
        auto &disk = persistent();
        if (!disk.open()) {
            LOGW_TAG("Observatory", "telemetry store unavailable ({}); events stay in memory", disk.health().last_error);
            return;
        }
        struct utsname u {};
        const std::string kernel = uname(&u) == 0 ? u.release : "";
        // Model-level identity only: no serial numbers, IMEI or account data.
        const std::string device = property("ro.product.manufacturer") + " " + property("ro.product.model") + " " +
                                   property("ro.board.platform");
        auto io = flux::observatory::make_posix_io();
        const auto epoch = flux::observatory::load_or_create_epoch(
            *io, flux::observatory::kTelemetryRoot,
            {get_module_version(), property("ro.build.version.release"), kernel, device, property("ro.product.cpu.abi")},
            wall_ms(), random_hex32);
        if (epoch.epoch)
            LOGI_TAG("Observatory", "installation epoch {} ({})", epoch.epoch->installation_id,
                     epoch.created ? "created" : "preserved");
        else
            LOGW_TAG("Observatory", "installation epoch unavailable: {}", epoch.error);
        const auto r = disk.maintain();
        if (r.ran)
            LOGI_TAG("Observatory", "telemetry retention: {} segment(s) deleted, {} trimmed, {} record(s) removed",
                     r.segments_deleted, r.segments_trimmed, r.records_removed);
        else
            LOGW_TAG("Observatory", "telemetry retention skipped: {}", r.skipped_reason);
    } catch (const std::exception &e) {
        LOGW_TAG("Observatory", "telemetry start failed: {}", e.what());
    } catch (...) {
    }
}

void maintain_if_due() {
    try {
        auto &disk = persistent();
        if (disk.health().open && disk.maintenance_due()) {
            const auto r = disk.maintain();
            if (!r.ran) LOGW_TAG("Observatory", "telemetry retention skipped: {}", r.skipped_reason);
        }
    } catch (...) {
    }
}

int idle_timeout_ms() { return static_cast<int>(flux::observatory::kMaintenanceIntervalMs); }

} // namespace flux_observatory
