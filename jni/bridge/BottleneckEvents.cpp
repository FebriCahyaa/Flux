#include "BottleneckEvents.hpp"

namespace flux::bridge {

namespace {

namespace b = flux::bottleneck;
namespace o = flux::observatory;
constexpr size_t kMaxKeys = 64, kMaxValue = 512, kMaxReason = 512;

o::Confidence map(flux::context::Confidence c) {
    switch (c) {
    case flux::context::Confidence::High: return o::Confidence::High;
    case flux::context::Confidence::Medium: return o::Confidence::Medium;
    case flux::context::Confidence::Low: return o::Confidence::Low;
    case flux::context::Confidence::None: break;
    }
    return o::Confidence::Unknown;
}

std::string clip(std::string s, size_t n) {
    if (s.size() > n) s.resize(n);
    return s;
}

void put(o::StateSnapshot &m, const std::string &k, const std::string &v) {
    if (m.size() < kMaxKeys) m[k] = clip(v, kMaxValue);
}

void put_evidence(o::StateSnapshot &m, const b::Finding &f) {
    size_t i = 0;
    for (const auto &e : f.evidence) {
        if (i >= kMaxEvidencePerFinding) break;
        put(m, std::string("evidence.") + b::to_string(f.kind) + "." + std::to_string(i++),
            e.metric + "=" + e.value + (e.threshold.empty() ? "" : " [" + e.threshold + "]") + " (" + e.source + " @" +
                std::to_string(e.timestamp_ms) + ")");
    }
}

} // namespace

o::Event bottleneck_event(const b::BottleneckResult &r, int64_t wall_ms) {
    o::Event e;
    e.timestamp_ms = wall_ms;
    e.source = "bottleneck";
    e.type = "BOTTLENECK_ASSESSED";
    e.severity = o::Severity::Info;
    e.session_id = r.session_id;
    e.confidence = map(r.primary.confidence);
    e.result = o::Result::Ok; // the analysis completed; the finding may still be "unknown"
    const std::string primary = b::to_string(r.primary.kind);
    std::string reason = r.primary.kind == b::Kind::Unknown
                             ? "no finding with sufficient evidence" + (r.note.empty() ? std::string() : ": " + r.note)
                             : primary + " " + b::to_string(r.primary.rating) + (r.note.empty() ? "" : " (" + r.note + ")");
    e.reason = clip(reason, kMaxReason);

    put(e.after, "primary", primary);
    put(e.after, "rating", b::to_string(r.primary.rating));
    put(e.after, "confidence", o::to_string(e.confidence));
    put(e.after, "conflict", r.conflict ? "true" : "false");
    put(e.after, "samples", std::to_string(r.samples));
    put(e.after, "assessed_at_ms", std::to_string(r.timestamp_ms));
    if (!r.note.empty()) put(e.after, "note", r.note);
    std::string secondary;
    for (const auto &f : r.secondary)
        secondary += (secondary.empty() ? "" : ",") + std::string(b::to_string(f.kind)) + ":" + b::to_string(f.rating);
    put(e.after, "secondary", secondary.empty() ? "none" : secondary);
    put_evidence(e.after, r.primary);
    for (const auto &f : r.secondary) put_evidence(e.after, f);
    return e;
}

o::Event bottleneck_failure_event(const std::string &session_id, const std::string &error, int64_t wall_ms) {
    o::Event e;
    e.timestamp_ms = wall_ms;
    e.source = "bottleneck";
    e.type = "BOTTLENECK_ANALYSIS_FAILED";
    e.severity = o::Severity::Warning;
    e.session_id = session_id;
    e.confidence = o::Confidence::Unknown;
    e.result = o::Result::Failed;
    e.reason = clip("bottleneck analysis failed: " + (error.empty() ? std::string("reason unavailable") : error), kMaxReason);
    return e;
}

} // namespace flux::bridge
