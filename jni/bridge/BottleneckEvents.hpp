// Observatory events for completed bottleneck assessments (Step 8.10).
//
// BOTTLENECK_ASSESSED carries the whole result (primary, rating, confidence, conflict, secondary,
// bounded evidence) in `after`; BOTTLENECK_ANALYSIS_FAILED records an analysis that could not
// complete. Both are produced by source "bottleneck" inside a session.
#pragma once

#include "BottleneckResult.hpp"
#include "Event.hpp"

#include <string>

namespace flux::bridge {

inline constexpr size_t kMaxEvidencePerFinding = 8;

flux::observatory::Event bottleneck_event(const flux::bottleneck::BottleneckResult &r, int64_t wall_ms);
flux::observatory::Event bottleneck_failure_event(const std::string &session_id, const std::string &error,
                                                  int64_t wall_ms);

} // namespace flux::bridge
