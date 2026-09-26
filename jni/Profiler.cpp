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

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "Flux.hpp"
#include "FluxLog.hpp"
#include "Profiler.hpp"
#include "Write2File.hpp"

#include "DeviceMitigationStore.hpp"
#include "FluxConfigStore.hpp"

#include <mutex>

#include <FluxUtility.hpp>
#include <SynthesisCore.hpp>

void set_profiler_env_vars() {
    // Get preferences from config store
    auto prefs = config_store.get_preferences();

    // Clear all existing FLUX_* environment variables. Names are collected first:
    // unsetenv() shifts environ, so unsetting while iterating skips the next entry
    // and a stale FLUX_*_DISABLED would survive after the switch is turned back on.
    extern char **environ;
    std::vector<std::string> stale;
    for (char **env = environ; *env; ++env) {
        std::string_view entry(*env);
        size_t eq_pos = entry.find('=');
        if (entry.starts_with("FLUX_") && eq_pos != std::string_view::npos) {
            stale.emplace_back(entry.substr(0, eq_pos));
        }
    }
    for (const auto &name : stale) unsetenv(name.c_str());

    // Use cached mitigation items instead of re-evaluating rules
    auto mitigation_items = device_mitigation_store.get_cached_mitigation_items(prefs.use_device_mitigation);

    // Set environment variable for mitigation items
    for (const auto &item : mitigation_items) {
        std::string env_var = "FLUX_" + item;
        std::transform(env_var.begin(), env_var.end(), env_var.begin(), [](unsigned char c) {
            if (!std::isalnum(c) && c != '_') return '_';
            return static_cast<char>(std::toupper(c));
        });

        setenv(env_var.c_str(), "1", 1);
        LOGD_TAG("Profiler", "Set mitigation env var: {}", env_var);
    }

    // Flux Sched is on by default; the profiler only restores stock uclamp values when disabled
    if (!prefs.flux_sched) {
        setenv("FLUX_SCHED_DISABLED", "1", 1);
    }

    // Flux Boost: each part is on by default; disabled parts are only restored.
    if (!prefs.flux_vm) setenv("FLUX_VM_DISABLED", "1", 1);
    if (!prefs.flux_io) setenv("FLUX_IO_DISABLED", "1", 1);
    if (!prefs.game_priority) setenv("FLUX_PRIORITY_DISABLED", "1", 1);

    // Game tweaks
    if (!prefs.net_tweaks) setenv("FLUX_NET_DISABLED", "1", 1);
    if (!prefs.touch_tweaks) setenv("FLUX_TOUCH_DISABLED", "1", 1);
    if (prefs.game_refresh_rate) setenv("FLUX_REFRESH_ENABLED", "1", 1);
    if (!prefs.drop_caches) setenv("FLUX_DROP_CACHES_DISABLED", "1", 1);
    if (!prefs.surface_boost) setenv("FLUX_SURFACE_DISABLED", "1", 1);
    if (!prefs.chipset_boost) setenv("FLUX_CHIPSET_DISABLED", "1", 1);
    if (prefs.sustained_mode) setenv("FLUX_SUSTAINED", "1", 1);
    if (prefs.gpu_power_lock) setenv("FLUX_GPU_LOCK", "1", 1);

    // System tweaks (flux_profiler.sh system); off means the stock values are restored.
    if (prefs.adreno_reflex && !prefs.disable_tweaks) setenv("FLUX_REFLEX", "1", 1);
    if (prefs.graphics_tweaks && !prefs.disable_tweaks) setenv("FLUX_GRAPHICS", "1", 1);
    if (prefs.adaptive_refresh && !prefs.disable_tweaks) setenv("FLUX_ADAPTIVE_REFRESH", "1", 1);
    if (prefs.zram_tune && !prefs.disable_tweaks) setenv("FLUX_ZRAM", "1", 1);

    // Set CPU Governor variables
    FluxConfigStore::CPUGovernor cpu_governor_preference = config_store.get_cpu_governor();
    setenv("FLUX_BALANCED_CPUGOV", cpu_governor_preference.balance.c_str(), 1);
    setenv("FLUX_POWERSAVE_CPUGOV", cpu_governor_preference.powersave.c_str(), 1);

    // GPU governor: empty means "keep the kernel's governor" (the profiler restores it).
    const FluxConfigStore::GPUGovernor gpu_governor_preference = config_store.get_gpu_governor();
    setenv("FLUX_BALANCED_GPUGOV", gpu_governor_preference.balance.c_str(), 1);
    setenv("FLUX_POWERSAVE_GPUGOV", gpu_governor_preference.powersave.c_str(), 1);

    // Expose kernel and thermal API capabilities so the profiler shell script
    // can skip sysfs writes that are only valid on GKI kernels or API 31+.
    // Writing to unsupported sysfs nodes on certain vendor kernels can cause
    // hard hangs or kernel oops, leading to device freeze or reboot.
    {
        SynthesisCore status;
        if (synthesis_core_cache.get(status)) {
            setenv("FLUX_IS_GKI_KERNEL",        status.kernel_is_gki        ? "1" : "0", 1);
            setenv("FLUX_THERMAL_API_AVAILABLE", status.thermal_api_available ? "1" : "0", 1);
        } else {
            // Cache not yet populated — default to the safest / most-compatible values.
            setenv("FLUX_IS_GKI_KERNEL",        "0", 1);
            setenv("FLUX_THERMAL_API_AVAILABLE", "0", 1);
        }
    }
}

void apply_system_tweaks(bool force) {
    // Boot-time and on-change tweaks (zram, refresh range, graphics props). They run
    // even with tweaks disabled so that switching them off restores the stock values.
    static std::mutex mutex;
    static std::string applied;
    const auto prefs = config_store.get_preferences();
    const bool off = prefs.disable_tweaks;
    const std::string wanted = std::string(prefs.adreno_reflex && !off ? "r" : "-") +
                               (prefs.graphics_tweaks && !off ? "g" : "-") +
                               (prefs.adaptive_refresh && !off ? "a" : "-") + (prefs.zram_tune && !off ? "z" : "-");

    std::lock_guard lock(mutex);
    if (!force && wanted == applied) return;
    applied = wanted;

    set_profiler_env_vars();
    LOGI_TAG("Profiler", "Applying system tweaks ({})", wanted);
    if (system("flux_profiler system") != 0) {
        LOGE("Unable to execute profiler changes to system tweaks");
    }
}

void run_perfcommon(void) {
    if (config_store.get_preferences().disable_tweaks) {
        LOGI_TAG("Profiler", "Tweaks are disabled in config, skipping perfcommon");
        return;
    }

    set_profiler_env_vars();

    if (system("flux_profiler perfcommon")) {
        LOGE("Unable to execute profiler changes to perfcommon");
    }
}

void apply_performance_profile(bool lite_mode, std::string game_pkg, pid_t game_pid) {
    if (config_store.get_preferences().disable_tweaks) {
        LOGI_TAG("Profiler", "Tweaks are disabled in config, skipping performance profile");
        return;
    }

    set_profiler_env_vars();

    uid_t game_uid = 0;
    SynthesisCore status;
    if (synthesis_core_cache.get(status) && status.focused_app == game_pkg && status.focused_uid > 0) {
        game_uid = status.focused_uid;
    } else {
        game_uid = get_uid_by_package_name(game_pkg);
    }

    write2file(GAME_INFO, game_pkg, " ", game_pid, " ", game_uid, "\n");
    // The profiler raises this game's thread priority (Flux Boost) and restores it afterwards.
    setenv("FLUX_GAME_PID", std::to_string(game_pid).c_str(), 1);
    write2file(PROFILE_MODE, static_cast<int>(PERFORMANCE_PROFILE), "\n");

    if (lite_mode) {
        LOGD("Lite mode is enabled");
        if (system("flux_profiler performance_lite") != 0) {
            LOGE("Unable to execute profiler changes to performance_lite");
        }

        return;
    }

    if (system("flux_profiler performance") != 0) {
        LOGE("Unable to execute profiler changes to performance");
    }
}

void apply_performance_lite_profile(std::string game_pkg, pid_t game_pid) {
    if (config_store.get_preferences().disable_tweaks) {
        LOGI_TAG("Profiler", "Tweaks are disabled in config, skipping performance_lite profile");
        return;
    }

    set_profiler_env_vars();

    uid_t game_uid = 0;
    SynthesisCore status;
    if (synthesis_core_cache.get(status) && status.focused_app == game_pkg && status.focused_uid > 0) {
        game_uid = status.focused_uid;
    } else {
        game_uid = get_uid_by_package_name(game_pkg);
    }

    write2file(GAME_INFO, game_pkg, " ", game_pid, " ", game_uid, "\n");
    // The profiler raises this game's thread priority (Flux Boost) and restores it afterwards.
    setenv("FLUX_GAME_PID", std::to_string(game_pid).c_str(), 1);
    write2file(PROFILE_MODE, static_cast<int>(PERFORMANCE_LITE_PROFILE), "\n");

    LOGD("Thermal headroom low — applying performance_lite profile for {}", game_pkg);
    if (system("flux_profiler performance_lite") != 0) {
        LOGE("Unable to execute profiler changes to performance_lite");
    }
}

void apply_balance_profile() {
    if (config_store.get_preferences().disable_tweaks) {
        LOGI_TAG("Profiler", "Tweaks are disabled in config, skipping balance profile");
        return;
    }

    set_profiler_env_vars();

    write2file(GAME_INFO, "NULL 0 0\n");
    write2file(PROFILE_MODE, static_cast<int>(BALANCE_PROFILE), "\n");

    if (system("flux_profiler balance") != 0) {
        LOGE("Unable to execute profiler changes to balance");
    }
}

void apply_powersave_profile() {
    if (config_store.get_preferences().disable_tweaks) {
        LOGI_TAG("Profiler", "Tweaks are disabled in config, skipping powersave profile");
        return;
    }

    set_profiler_env_vars();

    write2file(GAME_INFO, "NULL 0 0\n");
    write2file(PROFILE_MODE, static_cast<int>(POWERSAVE_PROFILE), "\n");

    if (system("flux_profiler powersave") != 0) {
        LOGE("Unable to execute profiler changes to powersave");
    }
}
