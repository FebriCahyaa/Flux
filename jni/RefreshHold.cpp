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

#include "RefreshHold.hpp"

#include <cstdlib>
#include <sstream>

#include <sys/stat.h>

#include "include/Exec.hpp"

#ifndef FLUX_REFRESH_HOST_TEST
#include "FluxLog.hpp"
#else
#define LOGI(...) ((void)0)
#define LOGD(...) ((void)0)
#endif

namespace {

constexpr auto kHomeRecheck = std::chrono::minutes(10);
constexpr const char *kSystemUi = "com.android.systemui";

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

/// Refresh-rate setting as a number ("144", "144.0"); nullopt for null / garbage.
std::optional<float> rate_of(const std::string &v) {
    char *end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (end == v.c_str() || f < 1.0f || f > 1000.0f) return std::nullopt;
    return f;
}

bool exists(const char *path) {
    struct stat st{};
    return stat(path, &st) == 0;
}

RefreshHold::Io android_io() {
    return {
        .get_setting = [](const std::string &key) {
            return trim(flux::capture({"/system/bin/settings", "get", "system", key}, 256));
        },
        .put_setting = [](const std::string &key, const std::string &v) {
            flux::capture({"/system/bin/settings", "put", "system", key, v}, 256);
        },
        .home_package = [] {
            return parse_home_package(flux::capture({"/system/bin/cmd", "package", "resolve-activity", "--brief",
                                                     "-a", "android.intent.action.MAIN", "-c",
                                                     "android.intent.category.HOME"}, 4096));
        },
        .adaptive_applied = [] {
            // Written by flux_profiler.sh flux_adaptive_refresh when it widened the range;
            // /dev/.flux_refresh_orig exists while a game's refresh handling owns the setting.
            return exists("/data/adb/.config/flux/refresh_adaptive_orig") && !exists("/dev/.flux_refresh_orig");
        },
    };
}

} // namespace

std::string parse_home_package(const std::string &resolve_output) {
    // --brief prints a "priority=..." line, then "package/activity" on the last line.
    std::istringstream in(resolve_output);
    std::string line;
    std::string last;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) last = line;
    }
    const auto slash = last.find('/');
    if (slash == std::string::npos || slash == 0 || last.find(' ') != std::string::npos) return {};
    std::string pkg = last.substr(0, slash);
    // No default chosen: the chooser (ResolverActivity in "android") answers instead.
    if (pkg == "android") return {};
    return pkg;
}

RefreshHold &RefreshHold::get_instance() {
    static RefreshHold instance(android_io(), std::chrono::milliseconds(2000));
    return instance;
}

RefreshHold::RefreshHold(Io io, std::chrono::milliseconds release_delay)
    : io_(std::move(io)), release_delay_(release_delay), thread_(&RefreshHold::worker, this) {}

RefreshHold::~RefreshHold() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    release_locked();
}

bool RefreshHold::held() const {
    std::lock_guard lock(mutex_);
    return held_;
}

bool RefreshHold::is_holder(const std::string &package) {
    if (package.empty()) return false;
    if (package == kSystemUi) return true;
    const auto now = Clock::now();
    if (!home_checked_ || now - *home_checked_ > kHomeRecheck) {
        home_ = io_.home_package();
        home_checked_ = now;
    }
    return !home_.empty() && package == home_;
}

void RefreshHold::update(const std::string &focused_app, bool screen_on, bool in_game, bool enabled) {
    std::unique_lock lock(mutex_);
    if (!enabled || in_game || !screen_on) {
        if (!screen_on) home_checked_.reset();  // the launcher may have changed: check on wake
        release_at_.reset();
        release_locked();
        return;
    }
    if (is_holder(focused_app)) {
        release_at_.reset();
        if (!held_) hold_locked();
        return;
    }
    if (held_ && !release_at_) {
        // After the new app's opening animation, not during it.
        release_at_ = Clock::now() + release_delay_;
        lock.unlock();
        cv_.notify_all();
    }
}

void RefreshHold::release_now() {
    std::lock_guard lock(mutex_);
    release_at_.reset();
    release_locked();
}

void RefreshHold::hold_locked() {
    if (!io_.adaptive_applied()) return;
    const std::string min = io_.get_setting("min_refresh_rate");
    const std::string peak = io_.get_setting("peak_refresh_rate");
    const auto lo = rate_of(min);
    const auto hi = rate_of(peak);
    if (!lo || !hi || *lo >= *hi) return;  // already at the peak: nothing to hold
    io_.put_setting("min_refresh_rate", peak);
    held_ = true;
    low_ = min;
    peak_ = peak;
    LOGD("Refresh hold: launcher in focus, min {} → {} Hz", min, peak);
}

void RefreshHold::release_locked() {
    if (!held_) return;
    held_ = false;
    // Only undo our own change: if something else rewrote the minimum meanwhile
    // (a game's refresh handling, the user in Settings), leave it alone.
    if (io_.get_setting("min_refresh_rate") == peak_ && io_.adaptive_applied()) {
        io_.put_setting("min_refresh_rate", low_);
        LOGD("Refresh hold released: min back to {} Hz", low_);
    }
}

void RefreshHold::worker() {
    std::unique_lock lock(mutex_);
    while (!stop_) {
        if (release_at_) {
            if (cv_.wait_until(lock, *release_at_, [this] { return stop_ || !release_at_; })) continue;
            if (release_at_ && Clock::now() >= *release_at_) {
                release_at_.reset();
                release_locked();
            }
        } else {
            cv_.wait(lock, [this] { return stop_ || release_at_.has_value(); });
        }
    }
}
