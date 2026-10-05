// Zairenkai runtime metrics collector (Step 8.7) — read-only.
//
// Samples CPU, GPU, memory and storage metrics from procfs/sysfs through the read-only
// filesystem seam (flux::kernel::ReadOnlyFs: no write operation exists). Every metric carries
// value, timestamp, source, confidence, readable and verified. Missing, unreadable or malformed
// data is UNKNOWN; nothing is inferred (e.g. MemFree is never used in place of MemAvailable).
//
// Its only consumer in this step is the BottleneckModel input (to_runtime_sample). No policy,
// no optimisation, no kernel/GPU/thermal change.
#pragma once

#include "BottleneckModel.hpp"
#include "CapabilityContext.hpp"
#include "KernelIntelligence.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace flux::metrics {

struct Metric {
    std::string id;
    std::optional<double> value; // numeric value, nullopt when unknown or textual
    std::string text;            // textual value (scheduler, cluster cpus, interface list)
    std::string unit;            // "ratio", "mhz", "mb", "percent", "ms", ...
    int64_t timestamp_ms = 0;
    std::string source;          // node path or "derived:<ids>"
    flux::context::Confidence confidence = flux::context::Confidence::None;
    bool readable = false;       // a value was read and parsed
    bool verified = false;       // cross-checked (second source agrees / inside a declared range)
    std::string note;            // reason for Unknown, conflict, or derivation
};

struct MetricsSnapshot {
    int64_t timestamp_ms = 0;
    std::vector<Metric> metrics;
    const Metric *get(const std::string &id) const;
};

/// Stateful: utilisation, iowait and IO latency are deltas between two consecutive samples,
/// so the first sample reports them UNKNOWN ("needs a previous sample").
class RuntimeMetricsCollector {
  public:
    explicit RuntimeMetricsCollector(const flux::kernel::ReadOnlyFs &fs,
                                     const flux::context::CapabilityContext *context = nullptr);
    MetricsSnapshot sample(int64_t now_ms);

  private:
    struct CpuTimes {
        uint64_t busy = 0, total = 0, iowait = 0;
    };
    struct BlockStat {
        uint64_t ios = 0, ticks = 0;
    };
    const flux::kernel::ReadOnlyFs &fs_;
    const flux::context::CapabilityContext *context_;
    std::map<std::string, CpuTimes> prev_cpu_;
    std::map<std::string, BlockStat> prev_block_;
    bool have_prev_ = false;
};

/// Bottleneck input from a snapshot. Only readable metrics are used; Unknown stays nullopt.
/// fps / target come from the session (SessionRecorder), never from this collector.
flux::bottleneck::RuntimeSample to_runtime_sample(const MetricsSnapshot &s, std::optional<double> fps,
                                                  std::optional<double> target_hz);

// Cross-check tolerances.
inline constexpr double kFreqAgree = 0.10;    // relative
inline constexpr double kGpuBusyAgree = 15.0; // percentage points

} // namespace flux::metrics
