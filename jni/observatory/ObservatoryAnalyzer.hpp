// Observatory historical analysis (Step 8.12) — read-only over the persisted telemetry.
//
// Persisted events -> historical query (PersistentEventStore::query, index-assisted)
// -> session reconstruction (SessionTimeline) -> correlation -> explanations / summaries /
// repeated patterns. Deterministic: output depends only on the stored events and the explicit
// query range; no clock is read. Never writes telemetry, kernel, thermal or performance state,
// never re-rates bottleneck results, never repairs files.
#pragma once

#include "Explanation.hpp"
#include "SessionTimeline.hpp"
#include "TelemetryStore.hpp"

#include <optional>
#include <string>
#include <vector>

namespace flux::observatory {

/// FPS facts recorded in the session's bottleneck evidence. Hz and FPS are never mixed:
/// target/capability are display refresh (Hz); observed/shortfall are frames presented.
struct FpsSummary {
    std::optional<double> target_refresh_hz;      // display.refresh.current_hz evidence
    std::optional<double> refresh_capability_hz;  // display.refresh.max_hz evidence
    std::optional<double> observed_fps_peak;      // fps_vs_refresh "peak" (frames per second)
    std::optional<int> shortfall_samples, fps_samples; // frame_deficit "a/b samples below target"
    std::string source;                           // event the values came from
    std::vector<std::string> limitations;
};

struct SessionSummary {
    std::string session_id, package;      // "" = unknown
    std::optional<int64_t> duration_ms;
    std::string end_reason = "unknown";
    FpsSummary fps;
    std::string primary_bottleneck = "unknown", bottleneck_rating = "unknown", bottleneck_confidence = "unknown";
    std::string thermal = "no thermal evidence recorded";
    std::string transaction_status = "no transactions recorded";
    std::string restore_status = "no restore recorded";
    std::string completeness;
    std::vector<std::string> limitations;
};

struct SessionAnalysis {
    SessionTimeline timeline;
    SessionSummary summary;
    std::vector<Explanation> explanations; // lifecycle, transaction(s), bottleneck, thermal, fps
    uint64_t corrupted_lines = 0;
};

struct Pattern {
    std::string name; // repeated_cpu_bottleneck, repeated_gpu_bottleneck, repeated_thermal_constraint, repeated_restore_failure
    int occurrences = 0;
    int sessions_considered = 0;
    TimeRange range;
    Confidence confidence = Confidence::Unknown;
    std::vector<std::string> session_ids; // first kMaxPatternSessions
    std::vector<std::string> limitations;
};

struct HistoryReport {
    std::string package;
    TimeRange range;
    int sessions = 0, sessions_with_assessment = 0;
    std::vector<Pattern> patterns;
    std::vector<std::string> limitations;
    uint64_t corrupted_lines = 0;
};

inline constexpr size_t kMaxHistorySessions = 1000;
inline constexpr size_t kMaxPatternSessions = 20;

class ObservatoryAnalyzer {
  public:
    explicit ObservatoryAnalyzer(const PersistentEventStore &store) : store_(store) {}

    SessionTimeline timeline(const std::string &session_id, uint64_t *corrupted = nullptr) const;
    SessionAnalysis analyze(const std::string &session_id) const;
    /// Sessions of `package` that started in [from_ms, to_ms]; the range is clamped to the
    /// 7 x 24 h retention window ending at to_ms. Sessions are processed one at a time.
    HistoryReport history(const std::string &package, int64_t from_ms, int64_t to_ms) const;

  private:
    const PersistentEventStore &store_;
};

std::string to_text(const SessionAnalysis &a);
std::string to_text(const HistoryReport &h);

} // namespace flux::observatory
