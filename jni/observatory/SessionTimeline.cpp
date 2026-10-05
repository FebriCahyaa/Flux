#include "SessionTimeline.hpp"

#include <algorithm>
#include <sstream>

namespace flux::observatory {

const char *to_string(Phase p) {
    switch (p) {
    case Phase::Start: return "start";
    case Phase::Switch: return "switch";
    case Phase::Profile: return "profile";
    case Phase::Runtime: return "runtime";
    case Phase::Transaction: return "transaction";
    case Phase::Recovery: return "recovery";
    case Phase::Bottleneck: return "bottleneck";
    case Phase::End: return "end";
    case Phase::Other: break;
    }
    return "other";
}

Phase phase_of(const std::string &t) {
    if (t == "SESSION_START") return Phase::Start;
    if (t == "SESSION_SWITCH") return Phase::Switch;
    if (t == "SESSION_END") return Phase::End;
    if (t.rfind("PROFILE_", 0) == 0) return Phase::Profile;
    if (t.rfind("RUNTIME_", 0) == 0) return Phase::Runtime;
    if (t.rfind("TRANSACTION_", 0) == 0) return Phase::Transaction;
    if (t.rfind("RECOVERY_", 0) == 0) return Phase::Recovery;
    if (t.rfind("BOTTLENECK_", 0) == 0) return Phase::Bottleneck;
    return Phase::Other;
}

namespace {

std::string get(const StateSnapshot &m, const char *k) {
    auto it = m.find(k);
    return it == m.end() ? std::string() : it->second;
}

std::optional<int64_t> to_int(const std::string &s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        const long long v = std::stoll(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

bool before(const Event &a, const Event &b) { // EventStore ordering contract
    return a.timestamp_ms != b.timestamp_ms ? a.timestamp_ms < b.timestamp_ms : a.sequence < b.sequence;
}

} // namespace

SessionTimeline build_timeline(const std::string &session_id, const std::vector<Event> &session_events,
                               const std::vector<Event> &context_events) {
    SessionTimeline t;
    t.session_id = session_id;
    for (const auto &e : session_events) t.entries.push_back({phase_of(e.type), e, true});
    for (const auto &e : context_events) t.entries.push_back({phase_of(e.type), e, false});
    std::stable_sort(t.entries.begin(), t.entries.end(),
                     [](const TimelineEntry &a, const TimelineEntry &b) { return before(a.event, b.event); });

    for (const auto &x : t.entries) {
        if (!x.in_session) continue;
        const Event &e = x.event;
        switch (x.phase) {
        case Phase::Start:
            if (!t.has_start) t.start_ms = e.timestamp_ms;
            t.has_start = true;
            if (t.package.empty()) t.package = get(e.after, "package");
            break;
        case Phase::Switch: t.previous_session_id = get(e.before, "session_id"); break;
        case Phase::End:
            t.has_end = true;
            t.end_ms = e.timestamp_ms;
            t.end_reason = get(e.after, "end_reason");
            t.duration_ms = to_int(get(e.after, "duration_ms"));
            if (auto c = get(e.after, "clean"); c == "true" || c == "false") t.clean_end = c == "true";
            if (t.package.empty()) t.package = get(e.before, "package");
            break;
        case Phase::Profile: t.has_profile = true; break;
        case Phase::Runtime:
            t.has_runtime = true;
            if (t.package.empty()) t.package = get(e.after, "package");
            break;
        case Phase::Transaction:
            t.has_transactions = true;
            if (!e.transaction_id.empty() &&
                std::find(t.transaction_ids.begin(), t.transaction_ids.end(), e.transaction_id) == t.transaction_ids.end())
                t.transaction_ids.push_back(e.transaction_id);
            break;
        case Phase::Bottleneck: t.has_bottleneck = true; break;
        default: break;
        }
    }
    if (!t.duration_ms && t.start_ms && t.end_ms) t.duration_ms = *t.end_ms - *t.start_ms; // both recorded
    if (session_events.empty()) {
        t.limitations.push_back("no events recorded for this session (never persisted, or outside the 7 x 24 h window)");
        return t;
    }
    if (!t.has_start) t.limitations.push_back("SESSION_START not recorded (started before the retained window, or lost)");
    if (!t.has_end)
        t.limitations.push_back("SESSION_END not recorded (session still running, daemon stopped abruptly, or event lost)");
    if (t.has_end && !t.end_ms) t.limitations.push_back("SESSION_END without timestamp");
    return t;
}

std::string to_text(const SessionTimeline &t) {
    std::ostringstream o;
    o << "session: " << t.session_id << "\n"
      << "package: " << (t.package.empty() ? "unknown" : t.package) << "\n"
      << "start_ms: " << (t.start_ms ? std::to_string(*t.start_ms) : "unknown") << "\n"
      << "end_ms: " << (t.end_ms ? std::to_string(*t.end_ms) : "unknown") << "\n"
      << "end_reason: " << (t.end_reason.empty() ? "unknown" : t.end_reason) << "\n";
    if (!t.previous_session_id.empty()) o << "previous_session: " << t.previous_session_id << "\n";
    o << "entries:\n";
    for (const auto &x : t.entries) {
        o << "  " << x.event.timestamp_ms << " #" << x.event.sequence << " " << to_string(x.phase) << " " << x.event.type
          << " result=" << to_string(x.event.result);
        if (!x.event.transaction_id.empty()) o << " tx=" << x.event.transaction_id;
        if (!x.in_session) o << " (context: no session_id, inside session window)";
        o << " id=" << x.event.event_id << "\n";
    }
    for (const auto &l : t.limitations) o << "limitation: " << l << "\n";
    return o.str();
}

} // namespace flux::observatory
