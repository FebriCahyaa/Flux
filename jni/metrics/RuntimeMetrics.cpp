#include "RuntimeMetrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace flux::metrics {

namespace cx = flux::context;
using Kind = flux::kernel::ReadOnlyFs::Kind;

const Metric *MetricsSnapshot::get(const std::string &id) const {
    for (const auto &m : metrics)
        if (m.id == id) return &m;
    return nullptr;
}

namespace {

/// Result of reading one node: content, or why there is none.
struct Read {
    std::optional<std::string> content;
    std::string why; // "missing", "unreadable (permission or I/O)"
};

Read read(const flux::kernel::ReadOnlyFs &fs, const std::string &path) {
    if (fs.kind(path) == Kind::Missing) return {std::nullopt, path + " missing"};
    auto c = fs.read(path);
    if (!c) return {std::nullopt, path + " unreadable (permission or I/O)"};
    return {c, ""};
}

std::optional<double> number(const std::string &s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::nullopt;
    const char *start = s.c_str() + b;
    char *end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start || !std::isfinite(v)) return std::nullopt;
    return v;
}

/// Whole content must be one non-negative number (trailing whitespace allowed).
std::optional<double> strict_number(const std::string &s) {
    std::istringstream in(s);
    std::string tok, extra;
    if (!(in >> tok) || (in >> extra)) return std::nullopt;
    char *end = nullptr;
    const double v = std::strtod(tok.c_str(), &end);
    if (*end != '\0' || !std::isfinite(v) || v < 0) return std::nullopt;
    return v;
}

std::vector<std::string> words(const std::string &s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

bool pseudo_block(const std::string &dev) {
    for (const char *p : {"loop", "ram", "zram", "dm-"})
        if (dev.rfind(p, 0) == 0) return true;
    return false;
}

std::vector<int> cpu_list(const std::string &s) {
    std::vector<int> out;
    for (auto w : words(s)) {
        std::replace(w.begin(), w.end(), ',', ' ');
        for (const auto &part : words(w)) {
            auto dash = part.find('-');
            char *e = nullptr;
            long a = std::strtol(part.c_str(), &e, 10);
            if (e == part.c_str()) continue;
            long b = dash == std::string::npos ? a : std::strtol(part.c_str() + dash + 1, nullptr, 10);
            for (long i = a; i <= b && i - a < 1024; ++i) out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

class Builder {
  public:
    explicit Builder(int64_t ts) : ts_(ts) {}
    Metric &ok(const std::string &id, double value, const char *unit, const std::string &source,
               cx::Confidence c = cx::Confidence::High, bool verified = false, const std::string &note = "") {
        Metric m;
        m.id = id;
        m.value = value;
        m.unit = unit;
        m.timestamp_ms = ts_;
        m.source = source;
        m.confidence = c;
        m.readable = true;
        m.verified = verified;
        m.note = note;
        out.push_back(std::move(m));
        return out.back();
    }
    Metric &text(const std::string &id, const std::string &value, const std::string &source,
                 cx::Confidence c = cx::Confidence::High, const std::string &note = "") {
        Metric m;
        m.id = id;
        m.text = value;
        m.timestamp_ms = ts_;
        m.source = source;
        m.confidence = c;
        m.readable = true;
        m.note = note;
        out.push_back(std::move(m));
        return out.back();
    }
    void unknown(const std::string &id, const std::string &why, const char *unit = "") {
        Metric m;
        m.id = id;
        m.unit = unit;
        m.timestamp_ms = ts_;
        m.note = why.empty() ? "unknown" : why;
        out.push_back(std::move(m));
    }
    std::vector<Metric> out;

  private:
    int64_t ts_;
};

/// A numeric reading from one node with the reason when absent.
struct Reading {
    std::optional<double> value;
    std::string source, why;
};

Reading read_number(const flux::kernel::ReadOnlyFs &fs, const std::string &path, bool strict = true) {
    auto r = read(fs, path);
    if (!r.content) return {std::nullopt, path, r.why};
    auto v = strict ? strict_number(*r.content) : number(*r.content);
    if (!v) return {std::nullopt, path, path + " malformed"};
    return {v, path, ""};
}

std::string first_reason(const Reading &a, const Reading &b) {
    if (!a.why.empty() && a.why.find("missing") == std::string::npos) return a.why;
    if (!b.why.empty() && b.why.find("missing") == std::string::npos) return b.why;
    return a.why.empty() ? b.why : a.why;
}

} // namespace

RuntimeMetricsCollector::RuntimeMetricsCollector(const flux::kernel::ReadOnlyFs &fs, const cx::CapabilityContext *context)
    : fs_(fs), context_(context) {}

MetricsSnapshot RuntimeMetricsCollector::sample(int64_t now_ms) {
    Builder b(now_ms);

    // ---- CPU utilisation (deltas of /proc/stat) --------------------------------------
    std::map<std::string, CpuTimes> cpu;
    auto stat = read(fs_, "proc/stat");
    if (stat.content) {
        std::istringstream in(*stat.content);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("cpu", 0) != 0) continue;
            auto w = words(line);
            if (w.size() < 6) continue;
            std::vector<uint64_t> f;
            for (size_t i = 1; i < w.size() && i <= 8; ++i) {
                char *e = nullptr;
                auto v = std::strtoull(w[i].c_str(), &e, 10);
                if (*e != '\0') break;
                f.push_back(v);
            }
            if (f.size() < 5) continue;
            f.resize(8, 0);
            CpuTimes t;
            t.busy = f[0] + f[1] + f[2] + f[5] + f[6] + f[7];
            t.iowait = f[4];
            t.total = t.busy + f[3] + f[4];
            cpu[w[0]] = t;
        }
    }
    const std::string stat_why = !stat.content ? stat.why
                               : cpu.empty()   ? "proc/stat malformed (no cpu lines)"
                                               : "needs a previous sample";
    std::optional<double> busiest;
    std::string busiest_cpu;
    std::optional<double> total_util, iowait;
    if (have_prev_) {
        for (const auto &[name, t] : cpu) {
            auto p = prev_cpu_.find(name);
            if (p == prev_cpu_.end() || t.total <= p->second.total || t.busy < p->second.busy) continue;
            const double dt = double(t.total - p->second.total);
            const double u = double(t.busy - p->second.busy) / dt;
            if (name == "cpu") {
                total_util = u;
                iowait = double(t.iowait - std::min(t.iowait, p->second.iowait)) / dt;
            } else if (!busiest || u > *busiest) {
                busiest = u;
                busiest_cpu = name;
            }
        }
    }
    if (busiest)
        b.ok("cpu.utilization.busiest_core", *busiest, "ratio", "proc/stat").text = busiest_cpu;
    else
        b.unknown("cpu.utilization.busiest_core", have_prev_ && !cpu.empty() ? "no interval between samples" : stat_why, "ratio");
    if (total_util)
        b.ok("cpu.utilization.total", *total_util, "ratio", "proc/stat");
    else
        b.unknown("cpu.utilization.total", have_prev_ && !cpu.empty() ? "no interval between samples" : stat_why, "ratio");

    // ---- CPU clusters ----------------------------------------------------------------
    const std::string cf = "sys/devices/system/cpu/cpufreq";
    std::optional<double> busiest_ratio;
    std::string busiest_policy;
    cx::Confidence ratio_conf = cx::Confidence::None;
    bool ratio_verified = false;
    const int busiest_index = busiest_cpu.size() > 3 ? std::atoi(busiest_cpu.c_str() + 3) : -1;
    for (const auto &policy : fs_.list(cf)) {
        if (policy.rfind("policy", 0) != 0) continue;
        const std::string dir = cf + "/" + policy + "/";
        const std::string id = "cpu.cluster." + policy;
        auto cpus = read(fs_, dir + "related_cpus");
        const auto members = cpus.content ? cpu_list(*cpus.content) : std::vector<int>{};
        if (!members.empty()) {
            std::string t;
            for (int c : members) t += (t.empty() ? "" : " ") + std::to_string(c);
            b.text(id + ".cpus", t, dir + "related_cpus");
        } else {
            b.unknown(id + ".cpus", cpus.content ? dir + "related_cpus malformed" : cpus.why);
        }

        auto hw = read_number(fs_, dir + "cpuinfo_cur_freq");
        auto gov = read_number(fs_, dir + "scaling_cur_freq");
        auto max = read_number(fs_, dir + "cpuinfo_max_freq");
        auto min = read_number(fs_, dir + "cpuinfo_min_freq");
        std::optional<double> cur;
        cx::Confidence conf = cx::Confidence::High;
        bool verified = false;
        std::string source, note;
        if (hw.value && gov.value) {
            cur = hw.value;
            source = hw.source;
            const double rel = std::fabs(*hw.value - *gov.value) / std::max(*hw.value, 1.0);
            if (rel <= kFreqAgree) {
                verified = true;
                note = "cpuinfo_cur_freq and scaling_cur_freq agree";
            } else {
                conf = cx::Confidence::Medium;
                note = "conflict: cpuinfo_cur_freq=" + std::to_string(long(*hw.value)) +
                       " scaling_cur_freq=" + std::to_string(long(*gov.value)) + "; hardware reading kept";
            }
        } else if (hw.value || gov.value) {
            cur = hw.value ? hw.value : gov.value;
            source = hw.value ? hw.source : gov.source;
            if (max.value && min.value && *cur >= *min.value && *cur <= *max.value) {
                verified = true;
                note = "inside cpuinfo_min_freq..cpuinfo_max_freq";
            }
        }
        if (cur)
            b.ok(id + ".cur_mhz", *cur / 1000.0, "mhz", source, conf, verified, note);
        else
            b.unknown(id + ".cur_mhz", first_reason(gov, hw), "mhz");
        if (max.value)
            b.ok(id + ".max_mhz", *max.value / 1000.0, "mhz", max.source);
        else
            b.unknown(id + ".max_mhz", max.why, "mhz");

        if (busiest_index >= 0 && std::find(members.begin(), members.end(), busiest_index) != members.end()) {
            busiest_policy = policy;
            if (cur && max.value && *max.value > 0) {
                busiest_ratio = std::min(1.0, *cur / *max.value);
                ratio_conf = conf;
                ratio_verified = verified;
            }
        }
    }
    if (busiest_ratio)
        b.ok("cpu.freq_ratio.busiest_cluster", *busiest_ratio, "ratio",
             "derived:cpu.cluster." + busiest_policy + ".cur_mhz/max_mhz", ratio_conf, ratio_verified)
            .text = busiest_policy;
    else
        b.unknown("cpu.freq_ratio.busiest_cluster",
                  !busiest ? "busiest core unknown" : busiest_policy.empty() ? "cluster of the busiest core unknown"
                                                                            : "cluster frequency unknown",
                  "ratio");

    // ---- GPU -------------------------------------------------------------------------
    const std::string kg = "sys/class/kgsl/kgsl-3d0/";
    std::vector<std::string> gpu_ifaces, gpu_devfreq;
    if (fs_.kind("sys/class/kgsl/kgsl-3d0") != Kind::Missing) gpu_ifaces.push_back("kgsl");
    for (const auto &d : fs_.list("sys/class/devfreq"))
        if (d.find("gpu") != std::string::npos || d.find("mali") != std::string::npos) gpu_devfreq.push_back(d);
    if (!gpu_devfreq.empty()) gpu_ifaces.push_back("devfreq");
    if (fs_.kind("sys/class/misc/mali0") != Kind::Missing) gpu_ifaces.push_back("mali");
    if (fs_.kind("sys/kernel/ged/hal") != Kind::Missing) gpu_ifaces.push_back("ged");
    if (gpu_ifaces.empty()) {
        b.unknown("gpu.interfaces", "no GPU interface (kgsl, devfreq gpu/mali, mali0, ged) present");
    } else {
        std::string t;
        for (const auto &i : gpu_ifaces) t += (t.empty() ? "" : " ") + i;
        b.text("gpu.interfaces", t, "sysfs");
    }

    // Utilisation sources in priority order; the first readable answers, a second cross-checks.
    std::vector<Reading> util;
    std::string util_why;
    auto note_why = [&](const std::string &w) {
        if (util_why.empty() || util_why.find("missing") != std::string::npos) util_why = w;
    };
    {
        auto r = read(fs_, kg + "gpu_busy_percentage");
        if (r.content) {
            auto v = number(*r.content);
            if (v && *v >= 0 && *v <= 100) util.push_back({v, kg + "gpu_busy_percentage", ""});
            else note_why(kg + "gpu_busy_percentage malformed");
        } else if (!gpu_ifaces.empty()) {
            note_why(r.why);
        }
    }
    {
        auto r = read(fs_, kg + "gpubusy");
        if (r.content) {
            auto w = words(*r.content);
            std::optional<double> busy = w.size() == 2 ? strict_number(w[0]) : std::nullopt;
            std::optional<double> total = w.size() == 2 ? strict_number(w[1]) : std::nullopt;
            if (busy && total && *total > 0 && *busy <= *total)
                util.push_back({*busy / *total * 100.0, kg + "gpubusy", ""});
            else
                note_why(busy && total ? kg + "gpubusy has no interval yet" : kg + "gpubusy malformed");
        } else if (!gpu_ifaces.empty()) {
            note_why(r.why);
        }
    }
    {
        auto r = read(fs_, "sys/kernel/ged/hal/gpu_utilization");
        if (r.content) {
            auto v = number(*r.content);
            if (v && *v >= 0 && *v <= 100) util.push_back({v, "sys/kernel/ged/hal/gpu_utilization", ""});
            else note_why("sys/kernel/ged/hal/gpu_utilization malformed");
        }
    }
    for (const auto &d : gpu_devfreq) {
        auto r = read(fs_, "sys/class/devfreq/" + d + "/load");
        if (!r.content) continue;
        auto v = number(*r.content); // "NN@freq"
        if (v && *v >= 0 && *v <= 100) util.push_back({v, "sys/class/devfreq/" + d + "/load", ""});
        else note_why("sys/class/devfreq/" + d + "/load malformed");
    }
    if (util.empty()) {
        b.unknown("gpu.utilization", util_why.empty() ? "no GPU utilisation interface" : util_why, "percent");
    } else if (util.size() == 1) {
        b.ok("gpu.utilization", *util[0].value, "percent", util[0].source);
    } else {
        const double diff = std::fabs(*util[0].value - *util[1].value);
        if (diff <= kGpuBusyAgree)
            b.ok("gpu.utilization", *util[0].value, "percent", util[0].source, cx::Confidence::High, true,
                 "agrees with " + util[1].source);
        else
            b.ok("gpu.utilization", *util[0].value, "percent", util[0].source, cx::Confidence::Medium, false,
                 "conflict: " + util[1].source + " reports " + std::to_string(long(std::lround(*util[1].value))) +
                     "; first-priority source kept");
    }

    Reading gcur = read_number(fs_, kg + "gpuclk"), gmax = read_number(fs_, kg + "max_gpuclk");
    if (!gcur.value && !gpu_devfreq.empty()) gcur = read_number(fs_, "sys/class/devfreq/" + gpu_devfreq[0] + "/cur_freq");
    if (!gmax.value && !gpu_devfreq.empty()) gmax = read_number(fs_, "sys/class/devfreq/" + gpu_devfreq[0] + "/max_freq");
    const std::string no_gpu = "no GPU frequency interface";
    if (gcur.value) b.ok("gpu.freq.cur_mhz", *gcur.value / 1e6, "mhz", gcur.source);
    else b.unknown("gpu.freq.cur_mhz", gpu_ifaces.empty() ? no_gpu : gcur.why, "mhz");
    if (gmax.value) b.ok("gpu.freq.max_mhz", *gmax.value / 1e6, "mhz", gmax.source);
    else b.unknown("gpu.freq.max_mhz", gpu_ifaces.empty() ? no_gpu : gmax.why, "mhz");
    if (gcur.value && gmax.value && *gmax.value > 0)
        b.ok("gpu.freq_ratio", std::min(1.0, *gcur.value / *gmax.value), "ratio", "derived:gpu.freq.cur_mhz/max_mhz");
    else
        b.unknown("gpu.freq_ratio", gpu_ifaces.empty() ? no_gpu : "GPU current or max frequency unknown", "ratio");

    // ---- Memory ----------------------------------------------------------------------
    auto meminfo = read(fs_, "proc/meminfo");
    std::map<std::string, std::optional<double>> mi;
    if (meminfo.content) {
        std::istringstream in(*meminfo.content);
        std::string line;
        while (std::getline(in, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto w = words(line.substr(colon + 1));
            mi[line.substr(0, colon)] = w.empty() ? std::nullopt : strict_number(w[0]);
        }
    }
    auto mem = [&](const char *id, const char *key) {
        if (!meminfo.content) return b.unknown(id, meminfo.why, "mb");
        auto it = mi.find(key);
        if (it == mi.end()) return b.unknown(id, std::string(key) + " not reported by proc/meminfo (not derived)", "mb");
        if (!it->second) return b.unknown(id, std::string("proc/meminfo ") + key + " malformed", "mb");
        b.ok(id, *it->second / 1024.0, "mb", std::string("proc/meminfo:") + key);
    };
    mem("mem.total_mb", "MemTotal");
    mem("mem.available_mb", "MemAvailable");
    mem("swap.total_mb", "SwapTotal");
    mem("swap.free_mb", "SwapFree");

    auto psi = [&](const char *id, const std::string &path, const char *which) {
        auto r = read(fs_, path);
        if (!r.content) return b.unknown(id, r.why, "percent");
        std::istringstream in(*r.content);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(which, 0) != 0) continue;
            auto at = line.find("avg10=");
            if (at == std::string::npos) break;
            auto w = words(line.substr(at + 6));
            auto v = w.empty() ? std::nullopt : strict_number(w[0]);
            if (!v) break;
            return (void)b.ok(id, *v, "percent", path + ":" + which + " avg10");
        }
        b.unknown(id, path + " malformed", "percent");
    };
    psi("mem.psi_some_avg10", "proc/pressure/memory", "some");
    psi("mem.psi_full_avg10", "proc/pressure/memory", "full");

    for (const auto &dev : fs_.list("sys/block")) {
        if (dev.rfind("zram", 0) != 0) continue;
        const std::string path = "sys/block/" + dev + "/mm_stat";
        auto r = read(fs_, path);
        const std::string id = "zram." + dev;
        if (!r.content) {
            if (fs_.kind(path) == Kind::Missing) continue;
            b.unknown(id + ".orig_mb", r.why, "mb");
            b.unknown(id + ".compr_mb", r.why, "mb");
            continue;
        }
        auto w = words(*r.content);
        auto orig = w.size() >= 3 ? strict_number(w[0]) : std::nullopt;
        auto compr = w.size() >= 3 ? strict_number(w[1]) : std::nullopt;
        if (orig) b.ok(id + ".orig_mb", *orig / 1048576.0, "mb", path + ":orig_data_size");
        else b.unknown(id + ".orig_mb", path + " malformed", "mb");
        if (compr) b.ok(id + ".compr_mb", *compr / 1048576.0, "mb", path + ":compr_data_size");
        else b.unknown(id + ".compr_mb", path + " malformed", "mb");
    }

    // ---- Storage ---------------------------------------------------------------------
    psi("io.psi_some_avg10", "proc/pressure/io", "some");
    if (iowait)
        b.ok("io.iowait_ratio", *iowait, "ratio", "proc/stat");
    else
        b.unknown("io.iowait_ratio", have_prev_ && !cpu.empty() ? "no interval between samples" : stat_why, "ratio");

    std::map<std::string, BlockStat> block;
    for (const auto &dev : fs_.list("sys/block")) {
        if (pseudo_block(dev)) continue;
        const std::string base = "sys/block/" + dev;
        // Scheduler: the kernel's fact when published, else the node itself (selector).
        std::string sched, sched_src;
        if (context_) {
            auto r = context_->resolve("io." + dev + ".scheduler");
            if (r.support == cx::Support::Yes && r.fact && r.fact->readable) {
                sched = r.fact->value;
                sched_src = "kernel_context";
            }
        }
        if (sched.empty()) {
            auto r = read(fs_, base + "/queue/scheduler");
            if (r.content) {
                auto open = r.content->find('['), close = r.content->find(']');
                if (open != std::string::npos && close != std::string::npos && close > open + 1) {
                    sched = r.content->substr(open + 1, close - open - 1);
                    sched_src = base + "/queue/scheduler";
                } else {
                    b.unknown("io." + dev + ".scheduler", base + "/queue/scheduler malformed");
                }
            }
        }
        if (!sched.empty()) b.text("io." + dev + ".scheduler", sched, sched_src);

        auto st = read(fs_, base + "/stat");
        if (!st.content) {
            if (fs_.kind(base + "/stat") != Kind::Missing) b.unknown("io." + dev + ".latency_ms", st.why, "ms");
            continue;
        }
        auto w = words(*st.content);
        std::vector<std::optional<double>> f;
        for (size_t i = 0; i < w.size() && i < 8; ++i) f.push_back(strict_number(w[i]));
        if (f.size() < 8 || !f[0] || !f[3] || !f[4] || !f[7]) {
            b.unknown("io." + dev + ".latency_ms", base + "/stat malformed", "ms");
            continue;
        }
        BlockStat now{uint64_t(*f[0] + *f[4]), uint64_t(*f[3] + *f[7])};
        block[dev] = now;
        auto p = prev_block_.find(dev);
        if (!have_prev_ || p == prev_block_.end()) {
            b.unknown("io." + dev + ".latency_ms", "needs a previous sample", "ms");
        } else if (now.ios <= p->second.ios || now.ticks < p->second.ticks) {
            b.unknown("io." + dev + ".latency_ms", "no completed I/O in the interval", "ms");
        } else {
            b.ok("io." + dev + ".latency_ms", double(now.ticks - p->second.ticks) / double(now.ios - p->second.ios), "ms",
                 base + "/stat", cx::Confidence::Medium, false, "average per completed request over the interval");
        }
    }

    prev_cpu_ = std::move(cpu);
    prev_block_ = std::move(block);
    have_prev_ = true;
    return {now_ms, std::move(b.out)};
}

flux::bottleneck::RuntimeSample to_runtime_sample(const MetricsSnapshot &s, std::optional<double> fps,
                                                  std::optional<double> target_hz) {
    auto value = [&](const char *id) -> std::optional<double> {
        auto *m = s.get(id);
        if (!m || !m->readable || !m->value || m->confidence == cx::Confidence::None) return std::nullopt;
        return m->value;
    };
    flux::bottleneck::RuntimeSample r;
    r.timestamp_ms = s.timestamp_ms;
    r.fps = fps;
    r.target_hz = target_hz;
    r.cpu_busiest_core = value("cpu.utilization.busiest_core");
    r.cpu_freq_ratio = value("cpu.freq_ratio.busiest_cluster");
    if (auto g = value("gpu.utilization")) r.gpu_busy = *g / 100.0;
    r.gpu_freq_ratio = value("gpu.freq_ratio");
    r.mem_psi_some = value("mem.psi_some_avg10");
    r.mem_available_mb = value("mem.available_mb");
    r.io_psi_some = value("io.psi_some_avg10");
    return r;
}

} // namespace flux::metrics
