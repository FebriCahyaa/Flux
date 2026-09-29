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

#include "GameRuntimeHost.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "DeviceMitigationStore.hpp"
#include "FluxConfigStore.hpp"
#include <Exec.hpp>
#include <Flux.hpp>
#include <FluxLog.hpp>
#include <GameRegistry.hpp>

extern GameRegistry game_registry;

namespace flux_runtime {

namespace {

flux::perf::RuntimeDeps make_deps() {
    flux::perf::RuntimeDeps d;
    d.node_io = flux::perf::make_node_io();
    d.files = flux::perf::make_file_store();
    d.paths = {GAME_PROFILES_FILE, PROFILE_LIBRARY_FILE, PERF_JOURNAL_FILE, LAUNCH_JOURNAL_FILE,
               LEGACY_COMPAT_JOURNAL_FILE};
    d.caps = flux::perf::probe_capabilities("", [] {
        return flux::perf::parse_panel_rates(flux::capture({"/system/bin/dumpsys", "display"}, 512 * 1024));
    });
    // Device mitigation rules may opt a device out per category (NO_GAME_MEMORY_OVERRIDE, ...).
    d.mitigation_allows = [](const std::string &category) {
        std::string key = "NO_GAME_" + category + "_OVERRIDE";
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::toupper(c); });
        const bool use = config_store.get_preferences().use_device_mitigation;
        return !device_mitigation_store.get_cached_mitigation_items(use).count(key);
    };
    d.gamelist_lite = [](const std::string &package) -> std::optional<bool> {
        if (const auto *g = game_registry.find_game_ptr(package)) return g->lite_mode;
        return std::nullopt;
    };
    d.log = [](const std::string &m) { LOGI_TAG("GameRuntime", "{}", m); };
    return d;
}

} // namespace

flux::perf::RuntimeHost &host() {
    static flux::perf::RuntimeHost instance(make_deps());
    return instance;
}

int refresh_request() { return host().refresh_request_hz(); }

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace flux_runtime
