// Neutral, consumable form of a completed bottleneck assessment (Step 8.10).
//
// A BottleneckResult is built once from a finished Assessment. It adds nothing: primary,
// secondary findings, ratings, confidence, conflict and evidence are copied from the
// assessment, so insufficient evidence stays primary = UNKNOWN. No policy reads it yet.
#pragma once

#include "BottleneckModel.hpp"

#include <string>
#include <vector>

namespace flux::bottleneck {

struct Finding {
    Kind kind = Kind::Unknown;
    State rating = State::Unknown;
    flux::context::Confidence confidence = flux::context::Confidence::None;
    std::string source;
    int64_t timestamp_ms = 0;
    std::vector<Evidence> evidence;
    std::string note;
};

struct BottleneckResult {
    std::string session_id;
    int64_t timestamp_ms = 0;  // when the assessment was made (session clock)
    size_t samples = 0;
    Finding primary;           // kind Unknown when no finding has sufficient evidence
    std::vector<Finding> secondary; // other categories rated POSSIBLE or higher
    bool conflict = false;
    std::string note;          // the assessment's explanation (conflict / insufficient data)
};

/// Pure copy of a completed assessment; never invents a finding.
BottleneckResult make_result(const Assessment &a, const std::string &session_id);

} // namespace flux::bottleneck
