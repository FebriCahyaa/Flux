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

#include "RenderBooster.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <map>
#include <string_view>
#include <unordered_set>

#include <dirent.h>
#include <sys/resource.h>

#ifndef FLUX_RENDER_HOST_TEST
#include "FluxLog.hpp"
#else
#define LOGI(...) ((void)0)
#define LOGD(...) ((void)0)
#define LOGW(...) ((void)0)
#endif

namespace {

constexpr auto kInterval = std::chrono::seconds(3);
constexpr int kRenderNice = -15;
constexpr int kRenderRtPriority = 15;
constexpr int kMaxCpus = CPU_SETSIZE;

/// Reads a small sysfs value as a positive number; 0 when missing or malformed.
long read_positive(const std::string &path) {
    std::ifstream in(path);
    std::string text;
    if (!in || !std::getline(in, text)) return 0;
    long value = 0;
    const char *end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc() || ptr == text.data() || value <= 0) return 0;
    return value;
}

std::string read_line(const std::string &path) {
    std::ifstream in(path);
    std::string text;
    if (in) std::getline(in, text);
    return text;
}

bool parse_int(std::string_view text, int &out) {
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && ptr == text.data() + text.size();
}

std::vector<pid_t> list_tids(pid_t pid) {
    std::vector<pid_t> tids;
    const std::string dir = "/proc/" + std::to_string(pid) + "/task";
    DIR *d = opendir(dir.c_str());
    if (!d) return tids;
    while (dirent *entry = readdir(d)) {
        int tid = 0;
        if (parse_int(entry->d_name, tid) && tid > 0) tids.push_back(tid);
    }
    closedir(d);
    return tids;
}

cpu_set_t to_set(const std::vector<int> &cpus) {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu : cpus) CPU_SET(cpu, &set);
    return set;
}

} // namespace

std::vector<int> parse_cpu_list(const std::string &list) {
    std::vector<int> cpus;
    size_t pos = 0;
    while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        if (comma == std::string::npos) comma = list.size();
        std::string_view part(list.data() + pos, comma - pos);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.remove_suffix(1);
        pos = comma + 1;

        int first = 0;
        int last = 0;
        const size_t dash = part.find('-');
        if (dash == std::string_view::npos) {
            if (!parse_int(part, first)) continue;
            last = first;
        } else if (!parse_int(part.substr(0, dash), first) || !parse_int(part.substr(dash + 1), last)) {
            continue;
        }
        if (first < 0 || last < first || last >= kMaxCpus) continue;
        for (int cpu = first; cpu <= last; ++cpu) cpus.push_back(cpu);
    }
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

std::vector<CpuCluster> detect_clusters(const std::string &root) {
    std::vector<int> cpus = parse_cpu_list(read_line(root + "/present"));
    if (cpus.empty()) cpus = parse_cpu_list(read_line(root + "/possible"));

    // cpu_capacity is what the scheduler itself uses; max frequency is the fallback
    // (it cannot tell apart two core types at the same clock, capacity can).
    bool use_capacity = !cpus.empty();
    for (int cpu : cpus) {
        if (read_positive(root + "/cpu" + std::to_string(cpu) + "/cpu_capacity") == 0) {
            use_capacity = false;
            break;
        }
    }

    std::map<long, std::vector<int>, std::greater<>> groups;
    for (int cpu : cpus) {
        const std::string base = root + "/cpu" + std::to_string(cpu);
        const long key = use_capacity ? read_positive(base + "/cpu_capacity")
                                      : read_positive(base + "/cpufreq/cpuinfo_max_freq");
        if (key > 0) groups[key].push_back(cpu);
    }

    std::vector<CpuCluster> clusters;
    for (auto &[capacity, members] : groups) clusters.push_back({capacity, std::move(members)});
    return clusters;
}

BoostMasks boost_masks(const std::vector<CpuCluster> &clusters) {
    BoostMasks masks;
    // One cluster (or unknown topology): every core is equal, affinity cannot help.
    if (clusters.size() < 2) return masks;

    // Fastest clusters until the render threads have two cores; never the slowest cluster.
    for (size_t i = 0; i + 1 < clusters.size(); ++i) {
        masks.render.insert(masks.render.end(), clusters[i].cpus.begin(), clusters[i].cpus.end());
        if (masks.render.size() >= 2) break;
    }

    for (size_t i = 0; i + 1 < clusters.size(); ++i)
        masks.game.insert(masks.game.end(), clusters[i].cpus.begin(), clusters[i].cpus.end());
    // Fewer than four fast cores would squeeze the game's worker threads together.
    if (masks.game.size() < 4) masks.game.clear();
    return masks;
}

bool is_render_thread(const std::string &comm) {
    // Thread names as the engines set them (at most 15 characters in comm).
    static constexpr std::string_view kPrefixes[] = {
        "UnityMain",      // Unity game logic / main loop
        "UnityGfxDevice", // Unity render thread (UnityGfxDeviceW)
        "GameThread",     // Unreal Engine game thread
        "RenderThread",   // Unreal "RenderThread 1", HWUI RenderThread
        "RHIThread",      // Unreal RHI submission thread
        "GLThread",       // GLSurfaceView (Cocos2d-x, many native games)
        "MainThread-UE",  // Unreal Engine 4 main thread on Android
    };
    return std::any_of(std::begin(kPrefixes), std::end(kPrefixes), [&](std::string_view prefix) {
        return comm.compare(0, prefix.size(), prefix) == 0;
    });
}

RenderBooster::~RenderBooster() {
    stop();
}

void RenderBooster::start(std::vector<pid_t> pids, bool realtime) {
    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    pids.erase(std::remove_if(pids.begin(), pids.end(), [](pid_t pid) { return pid <= 0; }), pids.end());
    if (pids.empty()) return;

    std::unique_lock lock(mutex_);
    if (running_) {
        const bool changed = pids_ != pids || realtime_ != realtime;
        pids_ = std::move(pids);
        realtime_ = realtime;
        lock.unlock();
        if (changed) cv_.notify_all();
        return;
    }
    if (thread_.joinable()) {
        lock.unlock();
        thread_.join();
        lock.lock();
    }
    pids_ = std::move(pids);
    realtime_ = realtime;
    lite_ = false;
    running_ = true;
    thread_ = std::thread(&RenderBooster::run, this);
}

void RenderBooster::stop() {
    std::unique_lock lock(mutex_);
    running_ = false;
    lock.unlock();
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void RenderBooster::set_lite(bool lite) {
    std::unique_lock lock(mutex_);
    if (lite_ == lite) return;
    lite_ = lite;
    lock.unlock();
    cv_.notify_all();
}

void RenderBooster::run() {
#ifndef FLUX_RENDER_HOST_TEST
    pthread_setname_np(pthread_self(), "RenderBooster");
#endif
    masks_ = boost_masks(detect_clusters());
    affinity_denied_ = false;
    priority_denied_ = false;
    if (masks_.render.empty()) {
        LOGI("Render booster: one CPU cluster, priorities only");
    } else {
        LOGI("Render booster: render threads on {} fast cores, game threads on {}", masks_.render.size(),
             masks_.game.empty() ? std::string("all cores") : std::to_string(masks_.game.size()) + " cores");
    }

    std::unique_lock lock(mutex_);
    while (running_) {
        const std::vector<pid_t> pids = pids_;
        const bool realtime = realtime_;
        const bool lite = lite_;
        lock.unlock();
        pass(pids, realtime, lite);
        lock.lock();
        // Wake early for stop, new PIDs or a Lite switch.
        cv_.wait_for(lock, kInterval, [&] {
            return !running_ || pids_ != pids || realtime_ != realtime || lite_ != lite;
        });
    }
    lock.unlock();
    restore_all();
}

void RenderBooster::pass(const std::vector<pid_t> &pids, bool realtime, bool lite) {
    std::unordered_set<pid_t> seen;
    for (pid_t pid : pids) {
        const std::vector<pid_t> tids = list_tids(pid);
        if (tids.empty()) continue; // gone, or /proc/<pid>/task not readable (SELinux)

        std::vector<std::pair<pid_t, bool>> threads;
        bool named_render = false;
        for (pid_t tid : tids) {
            const std::string comm = read_line("/proc/" + std::to_string(pid) + "/task/" + std::to_string(tid) + "/comm");
            const bool render = is_render_thread(comm);
            named_render |= render;
            threads.emplace_back(tid, render);
        }
        for (auto &[tid, render] : threads) {
            // Engines without named render threads (NativeActivity) draw on the main thread.
            if (!named_render && tid == pid) render = true;
            boost_thread(tid, render, realtime, lite);
            seen.insert(tid);
        }
    }

    // Threads that ended need no restore; forget them so the table stays small.
    for (auto it = saved_.begin(); it != saved_.end();) {
        it = seen.count(it->first) ? std::next(it) : saved_.erase(it);
    }
}

void RenderBooster::boost_thread(pid_t tid, bool render, bool realtime, bool lite) {
    auto it = saved_.find(tid);
    if (it == saved_.end()) {
        Original original;
        if (sched_getaffinity(tid, sizeof(original.mask), &original.mask) != 0) return;
        errno = 0;
        original.nice = getpriority(PRIO_PROCESS, static_cast<id_t>(tid));
        if (errno != 0) return;
        original.policy = sched_getscheduler(tid);
        if (original.policy < 0 || sched_getparam(tid, &original.param) != 0) return;
        it = saved_.emplace(tid, original).first;
    }
    Original &original = it->second;

    // Lite (thermal headroom low): render threads share the game cores, no prime-only pinning.
    const std::vector<int> &cpus = render && !lite ? masks_.render : masks_.game;
    if (!affinity_denied_ && !cpus.empty()) {
        const cpu_set_t set = to_set(cpus);
        if (sched_setaffinity(tid, sizeof(set), &set) != 0 && (errno == EPERM || errno == EACCES)) {
            affinity_denied_ = true;
            LOGW("Render booster: affinity blocked ({}), priorities only", strerror(errno));
        }
    }

    if (priority_denied_) return;
    const bool fifo_wanted = render && realtime && !lite && original.policy == SCHED_OTHER;
    if (render) {
        original.render = true;
        if (original.nice > kRenderNice && setpriority(PRIO_PROCESS, static_cast<id_t>(tid), kRenderNice) != 0 &&
            (errno == EPERM || errno == EACCES)) {
            priority_denied_ = true;
            LOGW("Render booster: priority blocked ({})", strerror(errno));
            return;
        }
    }

    const int current = sched_getscheduler(tid);
    if (current < 0) return;
    const bool is_fifo = (current & ~SCHED_RESET_ON_FORK) == SCHED_FIFO;
    if (fifo_wanted && !is_fifo) {
        // SCHED_RESET_ON_FORK: threads the game spawns from here start as normal threads.
        sched_param param{};
        param.sched_priority = kRenderRtPriority;
        if (sched_setscheduler(tid, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0 && (errno == EPERM || errno == EACCES)) {
            priority_denied_ = true;
            LOGW("Render booster: SCHED_FIFO blocked ({})", strerror(errno));
        }
    } else if (!fifo_wanted && is_fifo && original.policy == SCHED_OTHER) {
        // Realtime switched off or Lite: back to the thread's own policy.
        sched_setscheduler(tid, original.policy, &original.param);
    }
}

void RenderBooster::restore_all() {
    size_t restored = 0;
    for (auto &[tid, original] : saved_) {
        if (sched_setaffinity(tid, sizeof(original.mask), &original.mask) != 0 && errno == ESRCH) continue;
        if ((sched_getscheduler(tid) & ~SCHED_RESET_ON_FORK) != original.policy)
            sched_setscheduler(tid, original.policy, &original.param);
        if (original.render) setpriority(PRIO_PROCESS, static_cast<id_t>(tid), original.nice);
        ++restored;
    }
    saved_.clear();
    if (restored > 0) LOGD("Render booster: restored {} threads", restored);
}
