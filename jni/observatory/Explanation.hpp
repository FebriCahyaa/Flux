// Neutral derived explanation (Step 8.12). Every evidence reference points at a stored event
// (event_id + field) or is marked `derived` with the stored inputs named in `value`.
// Wording is correlational ("observed", "consistent with", "supported by"); causation is
// never claimed by this layer.
#pragma once

#include "Event.hpp"

#include <optional>
#include <string>
#include <vector>

namespace flux::observatory {

struct EvidenceRef {
    std::string event_id;
    std::string event_type;
    int64_t timestamp_ms = 0;
    std::string field; // e.g. "result", "after.primary", "after.evidence.cpu.0"
    std::string value;
    bool derived = false;
};

struct TimeRange {
    std::optional<int64_t> from_ms, to_ms;
};

struct Explanation {
    std::string topic; // lifecycle / transaction / bottleneck / thermal / fps
    std::string summary;
    std::string primary_finding;
    std::vector<EvidenceRef> supporting, contradicting;
    Confidence confidence = Confidence::Unknown;
    TimeRange time_range;
    std::string related_session_id;
    std::string related_transaction_id;
    std::vector<std::string> limitations;
};

std::string to_text(const Explanation &e);

/// One stored bottleneck evidence string: "metric=value [threshold] (source @ts)".
struct ParsedEvidence {
    bool ok = false;
    std::string metric, value, threshold, source;
    int64_t timestamp_ms = 0;
};
ParsedEvidence parse_evidence(const std::string &text);

} // namespace flux::observatory
