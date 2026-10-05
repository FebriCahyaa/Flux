#include "SynreiThermalAdapter.hpp"

#include <cstdlib>
#include <exception>
#include <map>
#include <sstream>

namespace flux::thermal {

namespace {

namespace cx = flux::context;
using Kind = flux::kernel::ReadOnlyFs::Kind;

constexpr int64_t kClockSkewS = 2; // `updated` may be this far ahead of our wall clock

std::map<std::string, std::string> parse_kv(const std::string &text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

std::optional<long long> to_int(const std::string &s) {
    if (s.empty()) return std::nullopt;
    char *end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (*end != '\0') return std::nullopt;
    return v;
}

TempReading temp(const std::map<std::string, std::string> &kv, const char *key) {
    TempReading r;
    auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) {
        r.note = std::string(key) + " not reported by Synrei";
        return r;
    }
    char *end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    if (*end != '\0' || end == it->second.c_str() || v < -40.0 || v > 150.0) {
        r.note = std::string(key) + " malformed (" + it->second + ")";
        return r;
    }
    r.celsius = v;
    r.readable = true;
    return r;
}

TempReading not_current(const char *key) {
    TempReading r;
    r.note = std::string(key) + " not current (context unverified)";
    return r;
}

} // namespace

SynreiThermalAdapter::SynreiThermalAdapter(const flux::kernel::ReadOnlyFs &fs, std::function<int64_t()> wall_clock_s,
                                           int64_t max_age_s)
    : fs_(fs), wall_clock_s_(std::move(wall_clock_s)), max_age_s_(max_age_s) {}

ThermalSnapshot SynreiThermalAdapter::read(int64_t now_ms) {
    ThermalSnapshot s;
    s.timestamp_ms = now_ms;
    s.source = std::string("synrei:/") + kSynreiStatePath;
    s.cpu = not_current("cpu_temp");
    s.gpu = not_current("gpu_temp");
    s.battery = not_current("battery_temp");
    try {
        if (fs_.kind(kSynreiStatePath) == Kind::Missing) {
            s.note = "Synrei not running (state file absent)";
            return s;
        }
        auto text = fs_.read(kSynreiStatePath);
        if (!text) {
            s.note = "Synrei state unreadable (permission or I/O)";
            return s;
        }
        const auto kv = parse_kv(*text);
        auto state = kv.find("state");
        if (state == kv.end() || state->second.empty()) {
            s.note = "Synrei state malformed (no state key)";
            return s;
        }
        s.readable = true;
        s.state = state->second;
        if (auto r = kv.find("reason"); r != kv.end()) s.reason = r->second;
        if (auto u = kv.find("updated"); u != kv.end()) s.source_time_s = to_int(u->second).value_or(0);
        int pid = 0;
        if (auto p = kv.find("pid"); p != kv.end()) pid = static_cast<int>(to_int(p->second).value_or(0));

        // Valid only when the publishing process is alive and the content is fresh.
        const int64_t wall = wall_clock_s_ ? wall_clock_s_() : 0;
        const bool alive = pid > 0 && fs_.kind("proc/" + std::to_string(pid)) != Kind::Missing;
        const bool fresh = s.source_time_s > 0 && wall > 0 && wall - s.source_time_s <= max_age_s_ &&
                           s.source_time_s <= wall + kClockSkewS;
        if (!alive) {
            s.note = "Synrei process not running (state file left behind); not current evidence";
            return s;
        }
        if (!fresh) {
            s.note = s.source_time_s > wall + kClockSkewS
                         ? "Synrei state timestamp is in the future; not current evidence"
                         : "stale: Synrei state is " + std::to_string(wall - s.source_time_s) + " s old; not current evidence";
            return s;
        }
        s.verified = true;
        s.confidence = cx::Confidence::High;
        s.cpu = temp(kv, "cpu_temp");
        s.gpu = temp(kv, "gpu_temp");
        s.battery = temp(kv, "battery_temp");
        // headroom_c: Synrei does not publish it; stays UNKNOWN.

        // Synrei's state decides the constraint; temperatures never do.
        if (s.state == "safety") s.constraint = Constraint::Constrained;
        else if (s.state == "boost") s.constraint = Constraint::Unconstrained;
        else s.note = "Synrei state '" + s.state + "' does not report a constraint";

        if (pid != previous_pid_) {
            previous_cpu_.reset();
            previous_pid_ = pid;
        }
        if (s.cpu.celsius && previous_cpu_ && now_ms > previous_ms_)
            s.slope_c_per_min = (*s.cpu.celsius - *previous_cpu_) / (double(now_ms - previous_ms_) / 60000.0);
        if (s.cpu.celsius) {
            previous_cpu_ = s.cpu.celsius;
            previous_ms_ = now_ms;
        }
    } catch (const std::exception &e) {
        ThermalSnapshot u;
        u.timestamp_ms = now_ms;
        u.source = s.source;
        u.cpu = not_current("cpu_temp");
        u.gpu = not_current("gpu_temp");
        u.battery = not_current("battery_temp");
        u.note = std::string("Synrei read failed: ") + e.what();
        return u;
    } catch (...) {
        ThermalSnapshot u;
        u.timestamp_ms = now_ms;
        u.source = s.source;
        u.note = "Synrei read failed";
        return u;
    }
    return s;
}

} // namespace flux::thermal
