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

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sched.h>
#include <sys/types.h>

/**
 * CPU clusters read from sysfs, fastest first. Cores are grouped by
 * cpu_capacity (EAS) or, without it, by cpuinfo_max_freq.
 */
struct CpuCluster {
    long capacity = 0;
    std::vector<int> cpus;
};

/// Parses a sysfs CPU list ("0-3,6,7"); malformed parts are skipped.
[[nodiscard]] std::vector<int> parse_cpu_list(const std::string &list);
/// Clusters under @p root (normally /sys/devices/system/cpu), fastest first.
[[nodiscard]] std::vector<CpuCluster> detect_clusters(const std::string &root = "/sys/devices/system/cpu");

/// CPU sets the booster uses, derived from the clusters.
struct BoostMasks {
    std::vector<int> render; ///< fastest clusters, at least two cores (render threads should not share one)
    std::vector<int> game;   ///< every cluster but the slowest, when that leaves at least four cores
};

[[nodiscard]] BoostMasks boost_masks(const std::vector<CpuCluster> &clusters);

/// True for thread names of engine render / game-logic threads (Unity, Unreal, GLSurfaceView, HWUI).
[[nodiscard]] bool is_render_thread(const std::string &comm);

/**
 * While a game runs: its render threads go to the fastest cores with nice -15
 * (optionally SCHED_FIFO 15), its other threads leave the little cores. The
 * pass repeats every 3 s for new threads; every thread gets its original
 * affinity, nice and policy back when the session ends.
 */
class RenderBooster {
public:
    static RenderBooster &get_instance() {
        static RenderBooster instance;
        return instance;
    }

    /// Start (or keep) boosting @p pids; @p realtime adds SCHED_FIFO for render threads.
    void start(std::vector<pid_t> pids, bool realtime);
    /// Restore every changed thread and stop (no-op when idle).
    void stop();
    /// Performance Lite: render threads share the big cores and stay SCHED_OTHER.
    void set_lite(bool lite);

    ~RenderBooster();
    RenderBooster(const RenderBooster &) = delete;
    RenderBooster &operator=(const RenderBooster &) = delete;

private:
    RenderBooster() = default;

    struct Original {
        cpu_set_t mask;
        int nice = 0;
        int policy = SCHED_OTHER;
        sched_param param{};
        bool render = false;
    };

    void run();
    void pass(const std::vector<pid_t> &pids, bool realtime, bool lite);
    void boost_thread(pid_t tid, bool render, bool realtime, bool lite);
    void restore_all();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    bool running_ = false;
    bool lite_ = false;
    bool realtime_ = false;
    std::vector<pid_t> pids_;

    // Worker thread only
    BoostMasks masks_;
    bool affinity_denied_ = false;
    bool priority_denied_ = false;
    std::unordered_map<pid_t, Original> saved_;
};
