/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

/**
 * Game session recorder: play time, frame rate and temperatures.
 *
 * While a game session runs, a sampler thread records once per second:
 *   - FPS of the game, from the best source the device has:
 *       MediaTek FPSGO (per game process), the Qualcomm display's measured
 *       frame rate, or SurfaceFlinger's page-flip counter
 *   - the hottest CPU thermal zone and the battery temperature
 *   - whether Flux had to drop to Performance Lite (thermal tier)
 * Samples are skipped while the screen is off.
 *
 * Output (read by the WebUI):
 *   SESSION_LIVE_FILE     live state, rewritten every second during a session
 *   SESSION_HISTORY_FILE  JSON array of finished sessions, newest first
 */

/// One second of a session. Unknown values are NaN.
struct SessionSample {
    float fps;
    float cpu_c;
    float battery_c;
    bool lite;
};

/// Summary of a list of samples (pure, see session_summarize).
struct SessionSummary {
    int samples = 0;         ///< seconds recorded
    int fps_samples = 0;     ///< seconds with a frame-rate reading
    float fps_avg = NAN;
    float fps_median = NAN;
    float fps_low1 = NAN;    ///< 1st percentile of per-second FPS ("1% low")
    float fps_min = NAN;
    int drops = 0;           ///< times FPS fell below 80% of the session median
    int drop_seconds = 0;    ///< seconds spent below that line
    float stability = NAN;   ///< % of seconds within 90% of the median
    float cpu_avg = NAN;
    float cpu_max = NAN;
    float battery_avg = NAN;
    float battery_max = NAN;
    int lite_seconds = 0;    ///< seconds in Performance Lite
};

[[nodiscard]] SessionSummary session_summarize(const std::vector<SessionSample> &samples);

class SessionRecorder {
public:
    static SessionRecorder &get_instance() {
        static SessionRecorder instance;
        return instance;
    }

    /// Start (or keep) the session for @p package; @p pids are the game's processes.
    void start(const std::string &package, std::vector<pid_t> pids);
    /// Finish the current session and append it to the history (no-op when idle).
    void stop();
    void set_lite(bool lite);
    void set_paused(bool paused);
    /// Hottest CPU zone averaged over the last few samples; NAN outside a session.
    [[nodiscard]] float recent_cpu_temp();

    ~SessionRecorder();
    SessionRecorder(const SessionRecorder &) = delete;
    SessionRecorder &operator=(const SessionRecorder &) = delete;

private:
    SessionRecorder() = default;

    void run();
    void write_live(const SessionSample &now) const;
    void finish_locked();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    bool running_ = false;
    bool paused_ = false;
    bool lite_ = false;

    std::string package_;
    std::vector<pid_t> pids_;
    int64_t start_ms_ = 0;
    std::string fps_source_;
    std::vector<SessionSample> samples_;
};
