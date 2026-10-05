// FPS observation bridge (Step 8.8.1) — read-only.
//
// SessionRecorder stays the only FPS measurement: after each per-second sample it publishes the
// value into an FpsObservationSlot (one assignment under a mutex). The runtime metrics sampler
// reads the latest observation and accepts it only when it is valid, fresh and newer than the
// one it used before. Nothing here measures frames, and FPS is never estimated from load.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace flux::metrics {

struct FpsObservation {
    int64_t timestamp_ms = 0;  // steady (monotonic) clock, same as the session clock
    std::optional<double> fps; // frames presented per second
    bool valid = false;        // the recorder had a frame-rate reading for that second
    std::string source;        // recorder source: fpsgo / game / display / surfaceflinger
};

/// Written by SessionRecorder's thread, read by the daemon thread. Holds only the latest value.
class FpsObservationSlot {
  public:
    void publish(int64_t timestamp_ms, double fps, const std::string &source) {
        FpsObservation o;
        o.timestamp_ms = timestamp_ms;
        o.valid = fps == fps && fps > 0; // NaN / non-positive = no reading
        if (o.valid) o.fps = fps;
        o.source = source;
        std::lock_guard lock(mutex_);
        value_ = std::move(o);
    }
    void clear() {
        std::lock_guard lock(mutex_);
        value_.reset();
    }
    std::optional<FpsObservation> latest() const {
        std::lock_guard lock(mutex_);
        return value_;
    }

  private:
    mutable std::mutex mutex_;
    std::optional<FpsObservation> value_;
};

/// Read-only access for consumers (the sampler).
using FpsSource = std::function<std::optional<FpsObservation>()>;

struct FpsAcceptance {
    std::optional<double> fps; // nullopt = UNKNOWN for this sample
    std::string reason;        // why it was not accepted, or "accepted"
};

/// Valid, not older than max_age_ms, not in the future, and newer than previous_ts.
FpsAcceptance accept_fps(const std::optional<FpsObservation> &o, int64_t now_ms, int64_t max_age_ms,
                         int64_t previous_ts);

} // namespace flux::metrics
