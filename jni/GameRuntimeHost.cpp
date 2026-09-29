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

#include <atomic>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <mutex>
#include <unistd.h>

#include "DeviceMitigationStore.hpp"
#include "FluxConfigStore.hpp"

#include <Flux.hpp>
#include <FluxLog.hpp>

namespace flux_runtime {

namespace {

using namespace flux::compat;

std::atomic<int> g_refresh_request{0};
std::mutex g_mutex; // begin/end/after_profile can come from the main loop and stop paths

std::string slurp(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.size() > (1u << 20)) return {}; // a document this large is not one of ours
    return s;
}

Io real_io() {
    Io io;
    io.exists = [](const std::string &p) { return access(p.c_str(), F_OK) == 0; };
    io.read = [](const std::string &p) -> std::optional<std::string> {
        std::ifstream f(p);
        if (!f) return std::nullopt;
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return s;
    };
    io.write = [](const std::string &p, const std::string &v) {
        std::ofstream f(p, std::ios::trunc);
        if (!f) return false;
        f << v;
        f.flush();
        return f.good();
    };
    io.write_atomic = [](const std::string &p, const std::string &v) {
        const std::string tmp = p + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f) return false;
            f << v;
            f.flush();
            if (!f.good()) return false;
        }
        return std::rename(tmp.c_str(), p.c_str()) == 0;
    };
    return io;
}

/// Internal block-device queues (UFS / NVMe / eMMC); removable SD and boot/rpmb are left alone,
/// the same rule flux_profiler.sh applies (flux_storage_class).
std::vector<std::string> internal_block_queues() {
    std::vector<std::string> out;
    DIR *d = opendir("/sys/block");
    if (!d) return out;
    while (dirent *e = readdir(d)) {
        const std::string n = e->d_name;
        const bool known = n.rfind("sd", 0) == 0 || n.rfind("nvme", 0) == 0 || n.rfind("mmcblk", 0) == 0;
        if (!known) continue;
        if (n.find("rpmb") != std::string::npos || n.find("boot") != std::string::npos) continue;
        const std::string dir = "/sys/block/" + n;
        if (n.rfind("mmcblk", 0) == 0 && slurp(dir + "/removable").rfind("1", 0) == 0) continue;
        if (access((dir + "/queue").c_str(), F_OK) == 0) out.push_back(dir + "/queue");
    }
    closedir(d);
    return out;
}

/// Device mitigation gate. Rules opt a device out with NO_GAME_<CATEGORY>_OVERRIDE
/// (NO_GAME_MEMORY_OVERRIDE, NO_GAME_TOUCH_OVERRIDE, NO_GAME_STORAGE_OVERRIDE, NO_LAUNCH_BOOST).
bool mitigation_allows(const std::string &category) {
    const bool use = config_store.get_preferences().use_device_mitigation;
    const auto items = device_mitigation_store.get_cached_mitigation_items(use);
    std::string up;
    for (char c : category) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::string item = category == "launch_boost" ? "NO_" + up : "NO_GAME_" + up + "_OVERRIDE";
    return items.find(item) == items.end();
}

SessionRuntime &runtime() {
    static NativeBackend native;
    // No Zygisk backend is registered: no provider consuming the spool ships with Flux, so
    // identity layers are reported Unavailable rather than staged for nobody.
    static SessionRuntime rt([] {
        SessionDeps d;
        d.runtime.io = real_io();
        d.runtime.native = &native;
        d.runtime.zygisk = nullptr;
        d.runtime.block_queues = internal_block_queues(); // enumerated once, on first use
        d.runtime.mitigation_allows = mitigation_allows;
        d.journal_path = COMPAT_JOURNAL_FILE;
        d.status_path = COMPAT_STATUS_FILE;
        d.inputs = [](const SessionKey &key, ResolvedInputs &out, std::string &err) {
            AnalyzeInputs in;
            in.capabilities_json = slurp(CAPABILITY_FILE); // written at boot by `fluxd capabilities`; never probed here
            in.library_json = slurp(COMPAT_LIBRARY_FILE);
            in.known_games_json = slurp(COMPAT_GAMES_FILE);
            in.profiles_json = slurp(COMPAT_PROFILES_FILE);
            // A game nobody profiled behaves exactly as before the Game Runtime existed.
            if (!build_inputs(key.package, std::nullopt, Mode::Real, in, out, err)) return false;
            return true;
        };
        d.set_refresh_request = [](int hz) { g_refresh_request.store(hz, std::memory_order_relaxed); };
        d.info = [](const std::string &m) { LOGI_TAG("GameRuntime", "{}", m); };
        d.warn = [](const std::string &m) { LOGW_TAG("GameRuntime", "{}", m); };
        d.debug = [](const std::string &m) { LOGD_TAG("GameRuntime", "{}", m); };
        return d;
    }());
    return rt;
}

} // namespace

void recover_at_boot() {
    std::lock_guard lock(g_mutex);
    runtime().recover();
}

void begin(const std::string &package, pid_t pid, uid_t uid) {
    std::lock_guard lock(g_mutex);
    // Tweaks disabled: Flux applies nothing, so the Game Runtime must not either.
    if (config_store.get_preferences().disable_tweaks) return;
    SessionRuntime &rt = runtime();
    if (rt.active() && rt.key() == SessionKey{package, static_cast<int>(pid), 0}) return;
    SessionKey key{package, static_cast<int>(pid), static_cast<int>(uid)};
    rt.begin(key);
}

void after_profile_applied() {
    std::lock_guard lock(g_mutex);
    runtime().after_profile_applied();
}

void ensure_perf_started() {
    std::lock_guard lock(g_mutex);
    runtime().ensure_perf_started();
}

void end(EndReason reason) {
    std::lock_guard lock(g_mutex);
    runtime().end(reason);
}

int refresh_request() { return g_refresh_request.load(std::memory_order_relaxed); }

} // namespace flux_runtime
