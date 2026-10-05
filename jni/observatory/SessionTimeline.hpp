// Session reconstruction from persisted Observatory events (Step 8.12) — read-only.
//
// A timeline is the session's own events (session_id match) in the EventStore order
// (timestamp, then sequence), plus context events that carry no session id (recovery) and fall
// inside the session's time window. Nothing is invented: a lifecycle step that was not recorded
// is reported as missing in `limitations`.
#pragma once

#include "Event.hpp"

#include <optional>
#include <string>
#include <vector>

namespace flux::observatory {

enum class Phase { Start, Switch, Profile, Runtime, Transaction, Recovery, Bottleneck, End, Other };
const char *to_string(Phase p);
Phase phase_of(const std::string &event_type);

struct TimelineEntry {
    Phase phase = Phase::Other;
    Event event;            // exactly as persisted
    bool in_session = true; // false: context event without session_id, inside the session window
};

struct SessionTimeline {
    std::string session_id;
    std::string package;     // "" = not recorded
    std::string end_reason;  // "" = not recorded (no SESSION_END)
    std::string previous_session_id; // from SESSION_SWITCH
    std::optional<int64_t> start_ms, end_ms, duration_ms;
    std::optional<bool> clean_end;
    bool has_start = false, has_end = false, has_profile = false, has_runtime = false, has_transactions = false,
         has_bottleneck = false;
    std::vector<TimelineEntry> entries;
    std::vector<std::string> transaction_ids; // order of first appearance
    std::vector<std::string> limitations;
    bool complete() const { return has_start && has_end; }
};

/// `session_events` and `context_events` must already be in EventStore order.
SessionTimeline build_timeline(const std::string &session_id, const std::vector<Event> &session_events,
                               const std::vector<Event> &context_events);

std::string to_text(const SessionTimeline &t);

} // namespace flux::observatory
