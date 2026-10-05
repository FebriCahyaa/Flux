// Installation epoch (Step 8.11): written once, preserved across module updates, never retained.
//
// <root>/installation.json (fluxd: /data/adb/.config/zairenkai/installation.json), outside the
// telemetry directory so retention can never reach it. Device identity is model-level only
// (manufacturer, model, board platform) — no serial numbers, IMEI or account data.
#pragma once

#include "TelemetryStore.hpp"

#include <functional>
#include <optional>
#include <string>

namespace flux::observatory {

struct InstallationEpoch {
    int format = 1;
    std::string installation_id;     // 32 hex characters, random
    int64_t installed_at_ms = 0;     // 0 = wall clock was not set at creation
    std::string first_version;
    std::string first_android_version;
    std::string first_kernel_version;
    std::string first_device_identity;
    std::string architecture;
};

struct EpochFacts {
    std::string version, android_version, kernel_version, device_identity, architecture;
};

struct EpochResult {
    std::optional<InstallationEpoch> epoch;
    bool created = false;
    std::string error; // corrupt existing file is preserved and reported, never overwritten
};

/// Loads <root>/installation.json, or creates it once when absent.
EpochResult load_or_create_epoch(FileIo &io, const std::string &root, const EpochFacts &facts, int64_t wall_ms,
                                 const std::function<std::string()> &random_hex32);

std::string epoch_to_json(const InstallationEpoch &e);
std::optional<InstallationEpoch> epoch_from_json(const std::string &text);

} // namespace flux::observatory
