// Adaptive Optimization V1 (Phase 5) — intervention evaluation.
//
// Pure, deterministic comparison of a baseline window with a post-intervention window. Inputs are
// the samples the existing RuntimeMetricsSampler already produced (FPS from the SessionRecorder
// slot, CPU/GPU load and clock ratios), the Synrei snapshot it recorded and the live bottleneck
// assessment. Nothing is collected, written or persisted here. UNKNOWN stays UNKNOWN: a missing
// value is never treated as zero. Refresh (Hz) and FPS stay separate values.
#pragma once

#include "BottleneckModel.hpp"
#include "ThermalContext.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zairenkai::adaptive {

/// Synrei as already reported, never re-derived from temperatures.
enum class Thermal { Unknown, Safety, Boost, Other };
const char *to_string(Thermal t);
/// verified + readable snapshot: its state; anything else (missing, stale, unverified): Unknown.
Thermal from_synrei(const std::optional<flux::thermal::ThermalSnapshot> &s);

struct Sample {
    int64_t timestamp_ms = 0;
    std::optional<double> fps, target_hz;           // FPS (frames) and target (Hz): never equated
    std::optional<double> cpu_busy, gpu_busy;       // 0..1
    std::optional<double> cpu_freq_ratio, gpu_freq_ratio; // 0..1 of max
    Thermal thermal = Thermal::Unknown;
    flux::bottleneck::Kind bottleneck = flux::bottleneck::Kind::Unknown;
    flux::bottleneck::State bottleneck_state = flux::bottleneck::State::Unknown;
    bool bottleneck_conflict = false;
};

/// Over the samples where a value is known (nullopt when none is): FPS and shortfall are medians
/// (a single spike is not evidence), loads and clock ratios are means.
struct WindowSummary {
    size_t samples = 0, fps_samples = 0, thermal_known = 0, safety = 0;
    std::optional<double> fps, shortfall; // shortfall: mean max(0, target - fps) / target
    std::optional<double> cpu_busy, gpu_busy, cpu_freq_ratio, gpu_freq_ratio;
};
WindowSummary summarize(const std::vector<Sample> &w);

enum class Verdict { Observe, Keep, Rollback };
const char *to_string(Verdict v);

struct Evaluation {
    Verdict verdict = Verdict::Observe;
    std::string reason;                 // stable machine-readable reason
    std::vector<std::string> evidence;  // human-readable facts the verdict rests on
    WindowSummary baseline, post;
    bool bottleneck_shifted = false;
};

// Thresholds (documented in ADAPTIVE_OPTIMIZATION.md).
inline constexpr size_t kMinBaselineFps = 3;   // baseline samples with FPS + target
inline constexpr size_t kMinPostFps = 3;       // post samples with FPS + target before judging
inline constexpr size_t kMaxPostSamples = 10;  // undecided after this many samples: rollback
inline constexpr double kMinShortfallGain = 0.05; // shortfall must drop by >= 5 points of target
inline constexpr double kMinFpsGain = 0.05;       // or mean FPS must rise by >= 5 %
inline constexpr double kCostLoadRise = 0.10;     // load rise (0..1) treated as cost without benefit

/// target: the resource the intervention addressed ("cpu"/"gpu"); mitigation: true for a
/// MITIGATE intervention (judged on thermal relief, never on FPS).
Evaluation evaluate(const std::vector<Sample> &baseline, const std::vector<Sample> &post, const std::string &target,
                    bool mitigation);

} // namespace zairenkai::adaptive
