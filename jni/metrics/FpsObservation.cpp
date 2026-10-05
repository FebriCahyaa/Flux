#include "FpsObservation.hpp"

namespace flux::metrics {

FpsAcceptance accept_fps(const std::optional<FpsObservation> &o, int64_t now_ms, int64_t max_age_ms,
                         int64_t previous_ts) {
    if (!o) return {std::nullopt, "no observation from SessionRecorder"};
    if (!o->valid || !o->fps) return {std::nullopt, "SessionRecorder had no frame-rate reading"};
    if (o->timestamp_ms > now_ms) return {std::nullopt, "observation timestamp is in the future"};
    if (now_ms - o->timestamp_ms > max_age_ms)
        return {std::nullopt, "stale: " + std::to_string(now_ms - o->timestamp_ms) + " ms old"};
    if (o->timestamp_ms <= previous_ts) return {std::nullopt, "observation not newer than the previous sample"};
    return {o->fps, "accepted"};
}

} // namespace flux::metrics
