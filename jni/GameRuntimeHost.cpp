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
#include <csignal>
#include <ctime>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/api-level.h>
#endif

#include "DeviceMitigationStore.hpp"
#include "FluxConfigStore.hpp"

#include <Flux.hpp>
#include <FluxLog.hpp>
#include <FluxUtility.hpp>

#include "Analyze.hpp"
#include "Arming.hpp"
#include "ZygiskBackend.hpp"

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

int64_t device_sdk() {
#ifdef __ANDROID__
    return android_get_device_api_level();
#else
    return 34;
#endif
}

std::string trim_nl(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

int64_t read_daemon_pid() { return std::atoll(trim_nl(slurp(DAEMON_PID_FILE)).c_str()); }

ArmingEnv real_env(bool from_cli) {
    ArmingEnv env;
    env.now_ms = [] {
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    };
    env.boot_id = [] { return trim_nl(slurp("/proc/sys/kernel/random/boot_id")); };
    // The daemon arms with its own pid; the CLI arms on the daemon's behalf, so it must name the
    // daemon, not itself (the CLI exits at once and its pid would make the plan look abandoned).
    env.daemon_pid = [from_cli]() -> int64_t { return from_cli ? read_daemon_pid() : static_cast<int64_t>(getpid()); };
    env.pid_alive = [](int64_t pid) { return pid > 1 && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM); };
    env.app_id_of = [](const std::string &pkg) { return static_cast<int>(get_uid_by_package_name(pkg) % 100000); };
    return env;
}

AnalyzeInputs load_docs() {
    AnalyzeInputs in;
    in.capabilities_json = slurp(CAPABILITY_FILE);
    in.library_json = slurp(COMPAT_LIBRARY_FILE);
    in.known_games_json = slurp(COMPAT_GAMES_FILE);
    in.profiles_json = slurp(COMPAT_PROFILES_FILE);
    return in;
}

Arming &arming() {
    static Arming a(real_io(), ArmingConfig{}, real_env(false), device_sdk());
    return a;
}

void refresh_optin(Arming &a) { a.set_user_enabled(access(COMPAT_ZYGISK_OPTIN_FILE, F_OK) == 0); }

SessionRuntime &runtime() {
    static NativeBackend native;
    static ZygiskBackend zygisk(arming());
    // The Zygisk backend is always registered; it reports Unavailable until the provider library is
    // installed and the user opted in, so identity layers are never staged for nobody.
    static SessionRuntime rt([] {
        SessionDeps d;
        d.runtime.io = real_io();
        d.runtime.native = &native;
        d.runtime.zygisk = &zygisk;
        d.runtime.block_queues = internal_block_queues(); // enumerated once, on first use
        d.runtime.mitigation_allows = mitigation_allows;
        d.journal_path = COMPAT_JOURNAL_FILE;
        d.status_path = COMPAT_STATUS_FILE;
        d.inputs = [](const SessionKey &key, ResolvedInputs &out, std::string &err) {
            AnalyzeInputs in = load_docs(); // capabilities.json is written at boot by `fluxd capabilities`; never probed here
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
    // The daemon pid the provider checks plans against.
    if (FILE *f = std::fopen(DAEMON_PID_FILE, "w")) {
        std::fprintf(f, "%d\n", static_cast<int>(getpid()));
        std::fclose(f);
    }
}

void arm_all_now() {
    std::lock_guard lock(g_mutex);
    Arming &a = arming();
    refresh_optin(a);
    const ArmReport r = arm_all(a, load_docs(), config_store.get_preferences().disable_tweaks);
    for (const auto &[pkg, why] : r.failed) LOGW_TAG("GameRuntime", "provider arm package={} failed: {}", pkg, why);
    if (!r.armed.empty()) LOGI_TAG("GameRuntime", "provider armed for {} package(s)", r.armed.size());
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    arming().disarm_all();
    std::remove(DAEMON_PID_FILE);
}

int arm_from_cli() {
    const int64_t dpid = read_daemon_pid();
    if (dpid <= 1 || kill(static_cast<pid_t>(dpid), 0) != 0) return -1;
    Arming a(real_io(), ArmingConfig{}, real_env(true), device_sdk());
    a.set_user_enabled(access(COMPAT_ZYGISK_OPTIN_FILE, F_OK) == 0);
    return static_cast<int>(arm_all(a, load_docs(), false).armed.size());
}

std::string provider_state() {
    Arming a(real_io(), ArmingConfig{}, real_env(true), device_sdk());
    a.set_user_enabled(access(COMPAT_ZYGISK_OPTIN_FILE, F_OK) == 0);
    return to_string(a.provider_state());
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
    // A session ending is a natural moment to renew the lease and pick up profile edits.
    if (reason != EndReason::DaemonStop && !config_store.get_preferences().disable_tweaks) {
        Arming &a = arming();
        refresh_optin(a);
        arm_all(a, load_docs(), false);
    }
}

int refresh_request() { return g_refresh_request.load(std::memory_order_relaxed); }

} // namespace flux_runtime
