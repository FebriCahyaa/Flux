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

#include "SessionRecorder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <dirent.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#ifndef FLUX_SESSION_HOST_TEST
#include "Flux.hpp"
#include "FluxLog.hpp"
#include <SynthesisCore.hpp>
#else
#define LOGI(...) ((void)0)
#define LOGD(...) ((void)0)
#define LOGW(...) ((void)0)
#define SESSION_LIVE_FILE "/tmp/flux_session_live.json"
#define SESSION_HISTORY_FILE "/tmp/flux_sessions.json"
#endif

namespace {

constexpr int kMinSessionSeconds = 20; ///< shorter sessions are not kept
constexpr size_t kHistoryMax = 30;
constexpr size_t kTimelineMax = 720;   ///< points per session (bucket width doubles past it)
constexpr size_t kRecentMax = 60;      ///< seconds of live history in the live file
constexpr float kDropRatio = 0.8f;     ///< below 80% of the median is a drop
constexpr float kStableRatio = 0.9f;   ///< within 90% of the median is stable

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string read_small(const std::string &path, size_t max = 64 * 1024) {
    std::ifstream f(path);
    if (!f) return {};
    std::string out(max, '\0');
    f.read(out.data(), static_cast<std::streamsize>(max));
    out.resize(static_cast<size_t>(f.gcount()));
    return out;
}

bool exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

std::vector<std::string> list_dir(const std::string &dir, const std::string &prefix) {
    std::vector<std::string> out;
    if (DIR *d = opendir(dir.c_str())) {
        while (const dirent *e = readdir(d)) {
            if (std::strncmp(e->d_name, prefix.c_str(), prefix.size()) == 0) out.push_back(dir + "/" + e->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool icontains(std::string haystack, std::string needle) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    return lower(std::move(haystack)).find(lower(std::move(needle))) != std::string::npos;
}

bool plausible_fps(float v) {
    return std::isfinite(v) && v >= 0.0f && v <= 240.0f;
}

// ── FPS sources ─────────────────────────────────────────────────────────────

/// Thread group of a thread, from /proc/<tid>/status.
pid_t tgid_of(pid_t tid) {
    std::istringstream in(read_small("/proc/" + std::to_string(tid) + "/status", 4096));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Tgid:", 0) == 0) return static_cast<pid_t>(std::atoi(line.c_str() + 5));
    }
    return 0;
}

/// MediaTek FPSGO: per render-thread frame rate. The header names the columns.
float fps_from_fpsgo(const std::vector<pid_t> &pids) {
    const std::string text = read_small("/sys/kernel/fpsgo/fstb/fpsgo_status");
    if (text.empty()) return NAN;
    std::istringstream in(text);
    std::string line;
    int col_tid = -1;
    int col_fps = -1;
    float best = NAN;
    while (std::getline(in, line)) {
        std::istringstream cols(line);
        std::vector<std::string> f;
        for (std::string w; cols >> w;) f.push_back(w);
        if (f.empty()) continue;
        if (col_fps < 0) {
            for (size_t i = 0; i < f.size(); ++i) {
                if (icontains(f[i], "tid") || f[i] == "pid") col_tid = col_tid < 0 ? static_cast<int>(i) : col_tid;
                if (icontains(f[i], "currentfps") || icontains(f[i], "curr_fps") || icontains(f[i], "cur_fps"))
                    col_fps = static_cast<int>(i);
            }
            continue;
        }
        if (col_tid < 0 || static_cast<size_t>(std::max(col_tid, col_fps)) >= f.size()) continue;
        const pid_t tid = static_cast<pid_t>(std::atoi(f[static_cast<size_t>(col_tid)].c_str()));
        if (tid <= 0 || std::find(pids.begin(), pids.end(), tgid_of(tid)) == pids.end()) continue;
        const float v = std::strtof(f[static_cast<size_t>(col_fps)].c_str(), nullptr);
        if (plausible_fps(v) && (!std::isfinite(best) || v > best)) best = v;
    }
    return best;
}

/// Output of a program run directly (no shell, so arguments are never re-parsed), at most @p max bytes.
std::string capture(const std::vector<std::string> &argv, size_t max = 64 * 1024) {
    int fds[2];
    if (pipe(fds) != 0) return {};
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return {};
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        std::vector<char *> args;
        for (const auto &a : argv) args.push_back(const_cast<char *>(a.c_str()));
        args.push_back(nullptr);
        execv(args[0], args.data());
        _exit(127);
    }
    close(fds[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        if (out.size() < max) out.append(buf, static_cast<size_t>(std::min<ssize_t>(n, static_cast<ssize_t>(max - out.size()))));
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return out;
}

/**
 * The game's own frame rate from SurfaceFlinger: frames of the game's layer presented in the
 * last second (`dumpsys SurfaceFlinger --latency <layer>`, the timestamps frame-rate overlays
 * use). Unlike the display rate it does not fall when the panel lowers its refresh rate on a
 * still screen (lobby, menus), and it counts only the game, not the whole display.
 */
class GameLayerFps {
public:
    float sample(const std::string &package) {
        const int64_t now = mono_ns();
        if (package != package_ || layer_.empty() || now >= next_lookup_) {
            package_ = package;
            layer_ = find_layer(package);
            next_lookup_ = now + 10'000'000'000LL; // layers change with activities: look again every 10 s
        }
        if (layer_.empty()) return NAN;

        const std::string out = capture({"/system/bin/dumpsys", "SurfaceFlinger", "--latency", layer_});
        const int64_t end = mono_ns(); // after the call: every reported frame is in the past
        std::istringstream in(out);
        std::string line;
        std::getline(in, line); // refresh period
        int frames = 0;
        int total = 0;
        long long first = INT64_MAX, last = 0;
        while (std::getline(in, line)) {
            long long desired = 0, actual = 0, ready = 0;
            if (std::sscanf(line.c_str(), "%lld %lld %lld", &desired, &actual, &ready) != 3) continue;
            if (actual <= 0 || actual == INT64_MAX) continue; // pending / fence not signalled
            ++total;
            if (actual > end - 1'000'000'000LL && actual <= end) {
                ++frames;
                first = std::min(first, actual);
                last = std::max(last, actual);
            }
        }
        if (total == 0) {
            next_lookup_ = 0; // wrong or gone layer: look it up again next time
            return NAN;
        }
        // SurfaceFlinger keeps the last 128 frames: above ~128 FPS all of them fall inside the
        // second, so the rate comes from their time span instead of their count.
        if (frames == total && frames > 1 && last > first) {
            return static_cast<float>(frames - 1) * 1e9f / static_cast<float>(last - first);
        }
        return static_cast<float>(frames);
    }

private:
    static int64_t mono_ns() {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
    }

    /// The game's rendering layer: a SurfaceView (games draw there) of the package, BLAST
    /// variant first on Android 12+, otherwise any layer of the package.
    static std::string find_layer(const std::string &package) {
        std::istringstream in(capture({"/system/bin/dumpsys", "SurfaceFlinger", "--list"}));
        std::string line, surface_blast, surface, other;
        while (std::getline(in, line)) {
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            if (line.find(package) == std::string::npos) continue;
            const bool sv = line.find("SurfaceView") != std::string::npos;
            const bool blast = line.find("BLAST") != std::string::npos;
            if (sv && blast && surface_blast.empty()) surface_blast = line;
            else if (sv && surface.empty()) surface = line;
            else if (other.empty() && line.find("Background") == std::string::npos) other = line;
        }
        return !surface_blast.empty() ? surface_blast : !surface.empty() ? surface : other;
    }

    std::string package_;
    std::string layer_;
    int64_t next_lookup_ = 0;
};

/**
 * Game refresh rate matching (Game tweaks -> refresh rate): the profiler raises the panel to its
 * highest mode when a game starts; this lowers it to the smallest mode at or above what the game
 * actually renders, e.g. 90 Hz for a game capped at 90 FPS on a 120 Hz panel: the same
 * smoothness for less power and heat. Every 30 s of the game's own frame rate:
 *   - well below the panel rate  -> the matching lower mode
 *   - at the panel rate          -> probe the next mode up (the game may want more, e.g. a
 *                                   lobby at 60 then a match at 90); if the game does not use
 *                                   it, go back and keep that rate for the rest of the session
 * Only while the profiler holds the refresh rate (/dev/.flux_refresh_orig: the user's setting,
 * restored when the game ends) and only from the game's own frames, never the panel rate.
 */
class RefreshMatcher {
public:
    void on_sample(float fps, const std::string &source) {
        if (source != "game" && source != "fpsgo") return;
        if (!exists("/dev/.flux_refresh_orig")) return;
        if (++seconds_ <= kWarmup) return;
        if (std::isfinite(fps)) window_.push_back(fps);
        if (window_.size() < kWindow) return;

        std::sort(window_.begin(), window_.end());
        const float p90 = window_[window_.size() * 9 / 10];
        window_.clear();
        if (rates_.empty()) load_rates();
        if (rates_.empty()) return;
        if (current_ == 0) current_ = rates_.back();

        // The game's highest rate is known: keep it for the rest of the session, so a lobby
        // does not drop the panel just before the next match needs it again.
        if (locked_) return;
        if (probe_from_ != 0) {
            const int back = probe_from_;
            probe_from_ = 0;
            if (p90 < static_cast<float>(back + kSlack)) {
                set(back, p90);
                locked_ = true; // the game does not go faster: this is its rate
            }
            return;
        }
        if (p90 < static_cast<float>(current_ - kSlack)) {
            const int target = pick(p90);
            if (target < current_) set(target, p90);
        } else if (!locked_) {
            const auto next = std::upper_bound(rates_.begin(), rates_.end(), current_);
            if (next != rates_.end()) {
                probe_from_ = current_;
                set(*next, p90);
            }
        }
    }

private:
    static constexpr int kWarmup = 15;      ///< s ignored after the game starts (loading)
    static constexpr size_t kWindow = 30;   ///< samples per decision
    static constexpr int kSlack = 3;        ///< FPS margin under a panel mode

    void load_rates() {
        std::istringstream in(capture({"/system/bin/dumpsys", "display"}, 512 * 1024));
        std::string word;
        while (in >> word) {
            const auto at = word.find("fps=");
            if (at == std::string::npos) continue;
            const int r = std::atoi(word.c_str() + at + 4);
            if (r >= 60 && r <= 240 && std::find(rates_.begin(), rates_.end(), r) == rates_.end()) rates_.push_back(r);
        }
        std::sort(rates_.begin(), rates_.end());
    }

    /// Smallest mode that still shows every frame (a 60 FPS game renders ~59.x: 60 Hz).
    int pick(float fps) const {
        for (const int r : rates_) {
            if (static_cast<float>(r) >= fps - 1.0f) return r;
        }
        return rates_.back();
    }

    void set(int rate, float fps) {
        const std::string v = std::to_string(rate);
        capture({"/system/bin/settings", "put", "system", "peak_refresh_rate", v});
        capture({"/system/bin/settings", "put", "system", "min_refresh_rate", v});
        if (probe_from_ != 0) LOGI("Refresh rate: trying {} Hz, the game is at the panel limit ({:.0f} FPS)", rate, fps);
        else LOGI("Refresh rate matched to the game: {} Hz (game at {:.0f} FPS)", rate, fps);
        current_ = rate;
    }

    std::vector<int> rates_;
    std::vector<float> window_;
    int seconds_ = 0;
    int current_ = 0;
    int probe_from_ = 0;
    bool locked_ = false;
};

/// Qualcomm SDE: frames committed to the display ("fps: 59.94 duration: ...").
float fps_from_sde() {
    float best = NAN;
    for (const auto &crtc : list_dir("/sys/class/drm", "sde-crtc-")) {
        const std::string text = read_small(crtc + "/measured_fps", 256);
        const auto at = text.find("fps:");
        if (at == std::string::npos) continue;
        const float v = std::strtof(text.c_str() + at + 4, nullptr);
        if (plausible_fps(v) && (!std::isfinite(best) || v > best)) best = v;
    }
    return best;
}

/// SurfaceFlinger page-flip counter (transaction 1013), as a rate between two calls.
class PageFlipCounter {
public:
    float sample() {
        const int64_t count = read_count();
        const int64_t t = now_ms();
        float fps = NAN;
        if (count >= 0 && last_count_ >= 0 && count >= last_count_ && t > last_ms_) {
            fps = static_cast<float>(count - last_count_) * 1000.0f / static_cast<float>(t - last_ms_);
        }
        last_count_ = count;
        last_ms_ = t;
        return plausible_fps(fps) ? fps : NAN;
    }

private:
    static int64_t read_count() {
        FILE *p = popen("/system/bin/service call SurfaceFlinger 1013 2>/dev/null", "r");
        if (!p) return -1;
        char buf[256] = {};
        const size_t n = fread(buf, 1, sizeof(buf) - 1, p);
        pclose(p);
        buf[n] = '\0';
        // "Result: Parcel(000d1b3a    '....')"
        const char *at = std::strstr(buf, "Parcel(");
        if (!at) return -1;
        char *end = nullptr;
        const long long v = std::strtoll(at + 7, &end, 16);
        return end == at + 7 ? -1 : static_cast<int64_t>(v);
    }

    int64_t last_count_ = -1;
    int64_t last_ms_ = 0;
};

// ── Temperatures ────────────────────────────────────────────────────────────

float normalize_temp(long raw) {
    if (raw > 1000) return static_cast<float>(raw) / 1000.0f; // millidegrees
    if (raw > 200) return static_cast<float>(raw) / 10.0f;    // decidegrees
    return static_cast<float>(raw);
}

std::vector<std::string> cpu_zones() {
    std::vector<std::string> out;
    for (const auto &zone : list_dir("/sys/class/thermal", "thermal_zone")) {
        std::string type = read_small(zone + "/type", 128);
        while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back()))) type.pop_back();
        if ((icontains(type, "cpu") || icontains(type, "cluster")) && !icontains(type, "gpu")) out.push_back(zone + "/temp");
    }
    return out;
}

float hottest(const std::vector<std::string> &nodes) {
    float best = NAN;
    for (const auto &n : nodes) {
        const std::string text = read_small(n, 64);
        if (text.empty()) continue;
        const float c = normalize_temp(std::atol(text.c_str()));
        if (c > 5.0f && c < 150.0f && (!std::isfinite(best) || c > best)) best = c;
    }
    return best;
}

float battery_temp() {
#ifndef FLUX_SESSION_HOST_TEST
    SynthesisCore status;
    if (synthesis_core_cache.get(status) && std::isfinite(status.battery_temp)) return status.battery_temp;
#endif
    const std::string text = read_small("/sys/class/power_supply/battery/temp", 64);
    if (text.empty()) return NAN;
    const float c = normalize_temp(std::atol(text.c_str()));
    return (c > 5.0f && c < 90.0f) ? c : NAN;
}

// ── JSON helpers ────────────────────────────────────────────────────────────

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

void num(Writer &w, float v, int decimals = 1) {
    if (!std::isfinite(v)) {
        w.Null();
        return;
    }
    const float scale = std::pow(10.0f, static_cast<float>(decimals));
    w.Double(static_cast<double>(std::round(v * scale) / scale));
}

void write_summary(Writer &w, const SessionSummary &s) {
    w.StartObject();
    w.Key("samples");
    w.Int(s.samples);
    w.Key("fps_samples");
    w.Int(s.fps_samples);
    w.Key("fps_avg");
    num(w, s.fps_avg);
    w.Key("fps_median");
    num(w, s.fps_median);
    w.Key("fps_low1");
    num(w, s.fps_low1);
    w.Key("fps_min");
    num(w, s.fps_min);
    w.Key("drops");
    w.Int(s.drops);
    w.Key("drop_seconds");
    w.Int(s.drop_seconds);
    w.Key("stability");
    num(w, s.stability, 0);
    w.Key("cpu_avg");
    num(w, s.cpu_avg);
    w.Key("cpu_max");
    num(w, s.cpu_max);
    w.Key("battery_avg");
    num(w, s.battery_avg);
    w.Key("battery_max");
    num(w, s.battery_max);
    w.Key("lite_seconds");
    w.Int(s.lite_seconds);
    w.EndObject();
}

/// Atomic replace: readers (the WebUI) never see a half-written file.
void write_file(const std::string &path, const std::string &content) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << content << '\n';
    }
    std::rename(tmp.c_str(), path.c_str());
}

/// Timeline buckets: [fps_avg, fps_min, cpu_max, battery, lite_seconds] per bucket.
struct Bucket {
    float fps_sum = 0;
    int fps_n = 0;
    float fps_min = NAN;
    float cpu = NAN;
    float battery = NAN;
    int lite = 0;
    int n = 0;

    void add(const SessionSample &s) {
        if (std::isfinite(s.fps)) {
            fps_sum += s.fps;
            ++fps_n;
            fps_min = std::isfinite(fps_min) ? std::min(fps_min, s.fps) : s.fps;
        }
        if (std::isfinite(s.cpu_c)) cpu = std::isfinite(cpu) ? std::max(cpu, s.cpu_c) : s.cpu_c;
        if (std::isfinite(s.battery_c)) battery = std::isfinite(battery) ? std::max(battery, s.battery_c) : s.battery_c;
        lite += s.lite ? 1 : 0;
        ++n;
    }
};

std::vector<Bucket> timeline(const std::vector<SessionSample> &samples, int &bucket_seconds) {
    bucket_seconds = 5;
    while (samples.size() / static_cast<size_t>(bucket_seconds) > kTimelineMax) bucket_seconds *= 2;
    std::vector<Bucket> out;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (i % static_cast<size_t>(bucket_seconds) == 0) out.emplace_back();
        out.back().add(samples[i]);
    }
    return out;
}

} // namespace

// ── Statistics ──────────────────────────────────────────────────────────────

SessionSummary session_summarize(const std::vector<SessionSample> &samples) {
    SessionSummary s;
    s.samples = static_cast<int>(samples.size());

    std::vector<float> fps;
    float cpu_sum = 0, bat_sum = 0;
    int cpu_n = 0, bat_n = 0;
    for (const auto &x : samples) {
        if (std::isfinite(x.fps)) fps.push_back(x.fps);
        if (std::isfinite(x.cpu_c)) {
            cpu_sum += x.cpu_c;
            ++cpu_n;
            s.cpu_max = std::isfinite(s.cpu_max) ? std::max(s.cpu_max, x.cpu_c) : x.cpu_c;
        }
        if (std::isfinite(x.battery_c)) {
            bat_sum += x.battery_c;
            ++bat_n;
            s.battery_max = std::isfinite(s.battery_max) ? std::max(s.battery_max, x.battery_c) : x.battery_c;
        }
        s.lite_seconds += x.lite ? 1 : 0;
    }
    if (cpu_n) s.cpu_avg = cpu_sum / static_cast<float>(cpu_n);
    if (bat_n) s.battery_avg = bat_sum / static_cast<float>(bat_n);

    s.fps_samples = static_cast<int>(fps.size());
    if (fps.size() < 5) return s; // too little to judge the frame rate

    std::vector<float> sorted = fps;
    std::sort(sorted.begin(), sorted.end());
    float sum = 0;
    for (float v : fps) sum += v;
    s.fps_avg = sum / static_cast<float>(fps.size());
    const size_t n = sorted.size();
    s.fps_median = (n % 2) ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0f;
    // Lowest 1% of seconds: the ceil(n/100)-th smallest value.
    s.fps_low1 = sorted[(n + 99) / 100 - 1];
    s.fps_min = sorted.front();

    // Drops are counted against the session's own median, so a 30 FPS game
    // running a steady 30 is not "dropping" while a 120 FPS game at 80 is.
    if (s.fps_median >= 10.0f) {
        const float drop_line = s.fps_median * kDropRatio;
        const float stable_line = s.fps_median * kStableRatio;
        bool below = false;
        int stable = 0;
        for (float v : fps) {
            if (v < drop_line) {
                ++s.drop_seconds;
                if (!below) ++s.drops;
                below = true;
            } else {
                below = false;
            }
            if (v >= stable_line) ++stable;
        }
        s.stability = 100.0f * static_cast<float>(stable) / static_cast<float>(fps.size());
    }
    return s;
}

// ── Recorder ────────────────────────────────────────────────────────────────

SessionRecorder::~SessionRecorder() {
    stop();
}

void SessionRecorder::start(const std::string &package, std::vector<pid_t> pids) {
    std::unique_lock lock(mutex_);
    if (running_ && package_ == package) {
        pids_ = std::move(pids); // same game (possibly restarted): keep the session
        return;
    }
    if (running_) finish_locked();
    if (thread_.joinable()) {
        lock.unlock();
        cv_.notify_all();
        thread_.join();
        lock.lock();
    }

    package_ = package;
    pids_ = std::move(pids);
    start_ms_ = now_ms();
    samples_.clear();
    fps_source_.clear();
    paused_ = false;
    lite_ = false;
    running_ = true;
    thread_ = std::thread(&SessionRecorder::run, this);
    LOGI("Session started for {}", package_);
}

void SessionRecorder::stop() {
    std::unique_lock lock(mutex_);
    if (running_) finish_locked();
    lock.unlock();
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void SessionRecorder::set_lite(bool lite) {
    std::lock_guard lock(mutex_);
    lite_ = lite;
}

void SessionRecorder::set_paused(bool paused) {
    std::lock_guard lock(mutex_);
    paused_ = paused;
}

void SessionRecorder::run() {
#ifndef FLUX_SESSION_HOST_TEST
    pthread_setname_np(pthread_self(), "SessionRecorder");
#endif
    const std::vector<std::string> cpu_nodes = cpu_zones();
    const bool has_fpsgo = exists("/sys/kernel/fpsgo/fstb/fpsgo_status");
    const bool has_sde = !list_dir("/sys/class/drm", "sde-crtc-").empty();
    PageFlipCounter page_flips;
    GameLayerFps game_layer;
    RefreshMatcher refresh;

    std::unique_lock lock(mutex_);
    while (running_) {
        cv_.wait_for(lock, std::chrono::seconds(1), [this] { return !running_; });
        if (!running_) break;
        if (paused_) continue;
        const std::vector<pid_t> pids = pids_;
        const std::string package = package_;
        const bool lite = lite_;
        lock.unlock();

        // Best source first; the next one only when the previous gives nothing.
        std::string source;
        float fps = NAN;
        if (has_fpsgo && std::isfinite(fps = fps_from_fpsgo(pids))) source = "fpsgo";
        if (!std::isfinite(fps) && std::isfinite(fps = game_layer.sample(package))) source = "game";
        if (!std::isfinite(fps) && has_sde && std::isfinite(fps = fps_from_sde())) source = "display";
        if (!std::isfinite(fps) && std::isfinite(fps = page_flips.sample())) source = "surfaceflinger";

        const SessionSample sample{fps, hottest(cpu_nodes), battery_temp(), lite};
        refresh.on_sample(fps, source);

        lock.lock();
        if (!running_) break;
        if (!source.empty()) fps_source_ = source;
        samples_.push_back(sample);
        write_live(sample);
    }
}

void SessionRecorder::write_live(const SessionSample &now) const {
    rapidjson::StringBuffer buf;
    Writer w(buf);
    w.StartObject();
    w.Key("active");
    w.Bool(true);
    w.Key("package");
    w.String(package_.c_str());
    w.Key("start");
    w.Int64(start_ms_);
    w.Key("elapsed");
    w.Int(static_cast<int>(samples_.size()));
    w.Key("fps_source");
    w.String(fps_source_.c_str());
    w.Key("lite");
    w.Bool(now.lite);
    w.Key("now");
    w.StartObject();
    w.Key("fps");
    num(w, now.fps);
    w.Key("cpu");
    num(w, now.cpu_c);
    w.Key("battery");
    num(w, now.battery_c);
    w.EndObject();
    w.Key("summary");
    write_summary(w, session_summarize(samples_));
    // Last minute, for the live charts: [fps, cpu, battery]
    w.Key("recent");
    w.StartArray();
    const size_t from = samples_.size() > kRecentMax ? samples_.size() - kRecentMax : 0;
    for (size_t i = from; i < samples_.size(); ++i) {
        w.StartArray();
        num(w, samples_[i].fps);
        num(w, samples_[i].cpu_c);
        num(w, samples_[i].battery_c);
        w.EndArray();
    }
    w.EndArray();
    w.EndObject();
    write_file(SESSION_LIVE_FILE, buf.GetString());
}

void SessionRecorder::finish_locked() {
    running_ = false;
    write_file(SESSION_LIVE_FILE, "{\"active\":false}");

    const int seconds = static_cast<int>(samples_.size());
    if (seconds < kMinSessionSeconds) {
        LOGD("Session for {} too short ({} s), not kept", package_, seconds);
        return;
    }

    const SessionSummary summary = session_summarize(samples_);
    int bucket_seconds = 5;
    const auto buckets = timeline(samples_, bucket_seconds);

    rapidjson::StringBuffer buf;
    Writer w(buf);
    w.StartObject();
    w.Key("id");
    w.Int64(start_ms_);
    w.Key("package");
    w.String(package_.c_str());
    w.Key("start");
    w.Int64(start_ms_);
    w.Key("duration");
    w.Int(static_cast<int>((now_ms() - start_ms_) / 1000));
    w.Key("fps_source");
    w.String(fps_source_.c_str());
    w.Key("summary");
    write_summary(w, summary);
    w.Key("bucket_seconds");
    w.Int(bucket_seconds);
    w.Key("timeline");
    w.StartArray();
    for (const auto &b : buckets) {
        w.StartArray();
        num(w, b.fps_n ? b.fps_sum / static_cast<float>(b.fps_n) : NAN);
        num(w, b.fps_min);
        num(w, b.cpu);
        num(w, b.battery);
        w.Int(b.lite);
        w.EndArray();
    }
    w.EndArray();
    w.EndObject();

    // Prepend to the history, keeping the newest kHistoryMax sessions.
    rapidjson::Document history;
    history.Parse(read_small(SESSION_HISTORY_FILE, 4 * 1024 * 1024).c_str());
    if (history.HasParseError() || !history.IsArray()) history.SetArray();
    rapidjson::Document entry;
    entry.Parse(buf.GetString());
    rapidjson::Document out;
    out.SetArray();
    auto &alloc = out.GetAllocator();
    out.PushBack(rapidjson::Value(entry, alloc), alloc);
    for (auto &old : history.GetArray()) {
        if (out.Size() >= kHistoryMax) break;
        if (old.IsObject()) out.PushBack(rapidjson::Value(old, alloc), alloc);
    }
    rapidjson::StringBuffer outbuf;
    Writer ow(outbuf);
    out.Accept(ow);
    write_file(SESSION_HISTORY_FILE, outbuf.GetString());

    LOGI("Session for {} saved: {} s, avg {:.1f} FPS, {} drops, CPU max {:.1f}C", package_, seconds,
         std::isfinite(summary.fps_avg) ? summary.fps_avg : 0.0f, summary.drops,
         std::isfinite(summary.cpu_max) ? summary.cpu_max : 0.0f);
}
