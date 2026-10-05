// Runtime metrics sampling lifecycle (Step 8.8) — read-only.
//
// SessionManager owns the lifecycle (SamplerParticipant: begin -> start, end -> stop).
// RuntimeMetricsSampler schedules measurements on the session tick, owns one collector per
// session (so the first sample reports delta metrics UNKNOWN), timestamps samples, keeps a
// bounded window and forwards readings to the BottleneckModel. BottleneckModel judges only.
// Nothing here applies policy or writes any node.
//
// No thread is created: sampling runs on the daemon's session tick, which exists only while a
// session is active. Interval and window are clamped.
#pragma once

#include "BottleneckModel.hpp"
#include "CapabilityContext.hpp"
#include "FpsObservation.hpp"
#include "KernelIntelligence.hpp"
#include "RuntimeMetrics.hpp"
#include "SessionManager.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace flux::metrics {

struct SamplerConfig {
    int64_t interval_ms = 2000;
    size_t window = 120; // samples kept for the bottleneck model
    int64_t fps_max_age_ms = 3000; // older FPS observations are UNKNOWN
};
inline constexpr int64_t kMinIntervalMs = 1000, kMaxIntervalMs = 60000;
inline constexpr size_t kMinWindow = 3, kMaxWindow = 900;
inline constexpr int kMaxConsecutiveFailures = 3;
inline constexpr int64_t kMinFpsAgeMs = 1000, kMaxFpsAgeMs = 10000;
SamplerConfig clamp(SamplerConfig c);

/// Future Observatory sample events (interface only; nothing stores them).
struct SampleNotice {
    std::string session_id;
    int64_t timestamp_ms = 0;
    uint64_t index = 0;      // 1 = first sample of the session
    int readable = 0, unknown = 0;
    bool failed = false;     // the collector threw; sample skipped
    std::string error;
};
using SampleObserver = std::function<void(const SampleNotice &)>;

class RuntimeMetricsSampler {
  public:
    enum class State { Idle, Running, Failed };

    RuntimeMetricsSampler(const flux::kernel::ReadOnlyFs &fs, const flux::context::CapabilityContext *context,
                          SamplerConfig config = {}, FpsSource fps = nullptr);

    /// False when already running for this session (duplicate ignored). A different session
    /// replaces the running one (its samples are dropped).
    bool start(const std::string &session_id, int64_t now_ms);
    /// Idempotent. Keeps the final assessment of the stopped session.
    void stop(int64_t now_ms);
    /// Samples when the interval elapsed. Never throws; repeated collector failures stop sampling.
    void tick(int64_t now_ms);

    State state() const { return state_; }
    bool running() const { return state_ == State::Running; }
    const std::string &session_id() const { return session_id_; }
    uint64_t samples_taken() const { return taken_; }
    const std::deque<flux::bottleneck::RuntimeSample> &window() const { return window_; }
    const std::optional<MetricsSnapshot> &last_snapshot() const { return last_; }

    /// BottleneckModel judgement over the current window (or the last session's at stop).
    flux::bottleneck::Assessment assess(int64_t now_ms, const flux::bottleneck::PerformanceState &perf = {},
                                        const flux::bottleneck::ThermalContext *thermal = nullptr) const;
    const std::optional<flux::bottleneck::Assessment> &final_assessment() const { return final_; }

    void set_observer(SampleObserver o) { observer_ = std::move(o); }
    const SamplerConfig &config() const { return config_; }

  private:
    void notify(const SampleNotice &n) const;
    const flux::kernel::ReadOnlyFs &fs_;
    const flux::context::CapabilityContext *context_;
    SamplerConfig config_;
    FpsSource fps_;
    int64_t last_fps_ts_ = 0; // newest accepted observation (ordering)
    SampleObserver observer_;
    State state_ = State::Idle;
    std::string session_id_;
    std::unique_ptr<RuntimeMetricsCollector> collector_; // one per session
    std::deque<flux::bottleneck::RuntimeSample> window_;
    std::optional<MetricsSnapshot> last_;
    std::optional<flux::bottleneck::Assessment> final_;
    int64_t last_sample_ms_ = 0;
    uint64_t taken_ = 0;
    int failures_ = 0;
};

/// Session lifecycle adapter: the only thing SessionManager knows about sampling.
/// Never reports an unclean end and never throws into the session.
class SamplerParticipant final : public flux::session::SessionParticipant {
  public:
    explicit SamplerParticipant(RuntimeMetricsSampler &sampler) : sampler_(sampler) {}
    const char *name() const override { return "runtime_metrics"; }
    void begin(const flux::session::SessionInfo &s) override;
    bool end(const flux::session::SessionInfo &s, flux::session::EndReason why) override;
    void tick(const flux::session::SessionInfo &s, int64_t now_ms) override;
    bool needs_tick() const override { return sampler_.running(); }

  private:
    RuntimeMetricsSampler &sampler_;
};

} // namespace flux::metrics
