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

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

/**
 * Adaptive refresh, launcher side.
 *
 * With adaptive refresh on, the panel drops to its lowest rate when the
 * screen is idle and a touch sends it back to the peak. On panels without
 * LTPO every such change is a display mode switch that costs a frame or two.
 * On the home screen that happens at the start of nearly every swipe (idle a
 * moment, touch, switch), which reads as a laggy launcher. Apps are scrolled
 * continuously, so they switch once and it does not show.
 *
 * While the launcher (home, recents, app drawer) or the notification shade
 * has focus, this raises min_refresh_rate to the peak so no switch can
 * happen there. It puts the adaptive minimum back once another app has had
 * focus for a short while, after that app's opening animation. A game
 * session, the screen turning off or the feature being switched off put it
 * back at once.
 *
 * If fluxd dies while holding, min_refresh_rate stays at the peak: the
 * vendor's own behaviour, and fluxd re-applies the adaptive range on start.
 */
class RefreshHold {
public:
    using Clock = std::chrono::steady_clock;

    /// Side effects, injectable for host tests.
    struct Io {
        std::function<std::string(const std::string &key)> get_setting;         ///< settings get system <key>
        std::function<void(const std::string &key, const std::string &v)> put_setting;
        std::function<std::string()> home_package;  ///< default launcher package, "" if unknown
        std::function<bool()> adaptive_applied;     ///< the adaptive range is in place, no game owns the rate
    };

    static RefreshHold &get_instance();

    /// Test / custom construction; the singleton uses the real Android I/O.
    RefreshHold(Io io, std::chrono::milliseconds release_delay);
    ~RefreshHold();
    RefreshHold(const RefreshHold &) = delete;
    RefreshHold &operator=(const RefreshHold &) = delete;

    /// Called on every status change from the main loop.
    void update(const std::string &focused_app, bool screen_on, bool in_game, bool enabled);
    /// Put the adaptive minimum back now (game start, daemon stop).
    void release_now();

    [[nodiscard]] bool held() const;
    /// True when @p package keeps the peak rate (launcher or System UI shade).
    [[nodiscard]] bool is_holder(const std::string &package);

private:
    void hold_locked();
    void release_locked();
    void worker();

    Io io_;
    std::chrono::milliseconds release_delay_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;

    bool held_ = false;
    std::string low_;   ///< min_refresh_rate before the hold (the adaptive minimum)
    std::string peak_;  ///< value written while holding
    std::optional<Clock::time_point> release_at_;

    std::string home_;
    std::optional<Clock::time_point> home_checked_;

    std::thread thread_;  ///< last: starts once every member above is initialised
};

/// Default launcher from `cmd package resolve-activity --brief` output ("" when none is set).
[[nodiscard]] std::string parse_home_package(const std::string &resolve_output);
