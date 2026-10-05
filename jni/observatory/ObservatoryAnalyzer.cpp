#include "ObservatoryAnalyzer.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace flux::observatory {

namespace {

std::string get(const StateSnapshot &m, const std::string &k) {
    auto it = m.find(k);
    return it == m.end() ? std::string() : it->second;
}

EvidenceRef ref(const Event &e, const std::string &field, const std::string &value) {
    return {e.event_id, e.type, e.timestamp_ms, field, value, false};
}

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

std::optional<double> to_double(const std::string &s) {
    char *end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return std::nullopt;
    return v;
}

Confidence from_rating(const std::string &r) {
    if (r == "confirmed") return Confidence::High;
    if (r == "likely") return Confidence::Medium;
    if (r == "possible") return Confidence::Low;
    return Confidence::Unknown;
}

int rank(const std::string &r) {
    return r == "confirmed" ? 3 : r == "likely" ? 2 : r == "possible" ? 1 : 0;
}

/// "cpu:likely,gpu:possible" -> {cpu: likely, ...}
std::vector<std::pair<std::string, std::string>> secondary_of(const Event &b) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(get(b.after, "secondary"));
    std::string item;
    while (std::getline(in, item, ',')) {
        auto c = item.find(':');
        if (c != std::string::npos) out.emplace_back(item.substr(0, c), item.substr(c + 1));
    }
    return out;
}

/// Evidence entries "evidence.<kind>.<n>" of one kind, in index order.
std::vector<std::pair<std::string, std::string>> evidence_of(const Event &b, const std::string &kind) {
    std::vector<std::pair<int, std::pair<std::string, std::string>>> tmp;
    const std::string prefix = "evidence." + kind + ".";
    for (const auto &[k, v] : b.after)
        if (k.rfind(prefix, 0) == 0) tmp.push_back({std::atoi(k.c_str() + prefix.size()), {k, v}});
    std::sort(tmp.begin(), tmp.end(), [](auto &a, auto &c) { return a.first < c.first; });
    std::vector<std::pair<std::string, std::string>> out;
    for (auto &x : tmp) out.push_back(x.second);
    return out;
}

const Event *last_of(const std::vector<const Event *> &events, const std::string &type) {
    const Event *found = nullptr;
    for (const auto *e : events)
        if (e->type == type) found = e;
    return found;
}

std::string restore_words(const Event &e) {
    const auto lost = get(e.after, "not_restored"), ok = get(e.after, "restored");
    if (e.result == Result::Ok) return "restored" + (ok.empty() ? std::string() : " (" + ok + " node(s), verified read-back)");
    if (e.result == Result::Partial) return "partial: " + (lost.empty() ? std::string("some") : lost) + " node(s) not restored";
    return "failed: " + (lost.empty() ? std::string("nodes") : lost + " node(s)") + " not restored";
}

const char *kTransitions =
    "Synrei state transitions are not recorded as Observatory events; only the bottleneck assessment's thermal evidence "
    "is available (states such as safety, boost, relaxed, idle, suspended, disabled cannot be listed)";

} // namespace

SessionTimeline ObservatoryAnalyzer::timeline(const std::string &session_id, uint64_t *corrupted) const {
    TelemetryQuery q;
    q.session_id = session_id;
    QueryStats st;
    const auto own = store_.query(q, &st);
    uint64_t bad = st.corrupted_lines;
    std::vector<Event> context;
    if (!own.empty()) {
        TelemetryQuery c;
        c.source = "recovery";
        c.from_ms = own.front().timestamp_ms;
        c.to_ms = own.back().timestamp_ms;
        for (const auto &e : own)
            if (e.type == "SESSION_END") c.to_ms = e.timestamp_ms;
        QueryStats st2;
        for (auto &e : store_.query(c, &st2))
            if (e.session_id.empty()) context.push_back(std::move(e));
        bad += st2.corrupted_lines;
    }
    if (corrupted) *corrupted = bad;
    return build_timeline(session_id, own, context);
}

SessionAnalysis ObservatoryAnalyzer::analyze(const std::string &session_id) const {
    SessionAnalysis a;
    a.timeline = timeline(session_id, &a.corrupted_lines);
    const auto &t = a.timeline;
    auto &s = a.summary;
    s.session_id = session_id;
    s.package = t.package;
    s.duration_ms = t.duration_ms;
    if (!t.end_reason.empty()) s.end_reason = t.end_reason;
    s.limitations = t.limitations;

    std::vector<const Event *> own, recovery;
    const Event *start = nullptr, *end = nullptr, *assessed = nullptr, *failed = nullptr, *profile = nullptr;
    for (const auto &x : t.entries) {
        if (!x.in_session) {
            recovery.push_back(&x.event);
            continue;
        }
        own.push_back(&x.event);
        if (x.event.type == "SESSION_START" && !start) start = &x.event;
        if (x.event.type == "SESSION_END") end = &x.event;
        if (x.event.type == "BOTTLENECK_ASSESSED") assessed = &x.event;
        if (x.event.type == "BOTTLENECK_ANALYSIS_FAILED") failed = &x.event;
        if (x.event.type == "PROFILE_APPLIED") profile = &x.event;
    }
    TimeRange window{t.start_ms, t.end_ms};
    if (!window.from_ms && !own.empty()) window.from_ms = own.front()->timestamp_ms;
    if (!window.to_ms && !own.empty()) window.to_ms = own.back()->timestamp_ms;

    // -- lifecycle ----------------------------------------------------------------------------
    {
        Explanation e;
        e.topic = "lifecycle";
        e.related_session_id = session_id;
        e.time_range = window;
        e.limitations = t.limitations;
        if (own.empty()) {
            e.summary = "No events are recorded for this session.";
            e.primary_finding = "unknown";
        } else {
            std::ostringstream o;
            o << "Session for " << (t.package.empty() ? "an unrecorded package" : t.package) << ": start "
              << (start ? "recorded" : "not recorded") << ", end " << (end ? "recorded (" + t.end_reason + ")" : "not recorded");
            if (t.clean_end) o << (*t.clean_end ? ", all participants ended cleanly" : ", a participant did not end cleanly");
            if (t.duration_ms) o << ", duration " << *t.duration_ms << " ms";
            if (!t.previous_session_id.empty()) o << ", replaced session " << t.previous_session_id;
            o << ".";
            e.summary = o.str();
            e.primary_finding = t.complete() ? "complete lifecycle" : "incomplete lifecycle";
            e.confidence = t.complete() ? Confidence::High : Confidence::Low;
            if (start) e.supporting.push_back(ref(*start, "after.package", get(start->after, "package")));
            if (end) e.supporting.push_back(ref(*end, "after.end_reason", t.end_reason));
            if (end && t.clean_end && !*t.clean_end) e.contradicting.push_back(ref(*end, "after.clean", "false"));
        }
        a.explanations.push_back(e);
    }

    // -- transactions ---------------------------------------------------------------------------
    std::vector<std::string> statuses, restores;
    if (t.transaction_ids.empty()) {
        Explanation e;
        e.topic = "transaction";
        e.related_session_id = session_id;
        e.summary = "No performance transaction is recorded for this session.";
        e.primary_finding = "none recorded";
        e.time_range = window;
        if (profile) e.limitations.push_back("PROFILE_APPLIED recorded without a transaction id");
        a.explanations.push_back(e);
    }
    for (const auto &tx : t.transaction_ids) {
        std::vector<const Event *> ev;
        for (const auto *x : own)
            if (x->transaction_id == tx) ev.push_back(x);
        Explanation e;
        e.topic = "transaction";
        e.related_session_id = session_id;
        e.related_transaction_id = tx;
        e.time_range = {ev.front()->timestamp_ms, ev.back()->timestamp_ms};
        std::string subject;
        for (const auto *x : ev)
            if (subject.empty()) subject = get(x->after, "subject");
        const Event *begin = last_of(ev, "TRANSACTION_BEGIN"), *apply = last_of(ev, "TRANSACTION_APPLY"),
                    *verify = last_of(ev, "TRANSACTION_VERIFY"), *rollback = last_of(ev, "TRANSACTION_ROLLBACK"),
                    *restore = last_of(ev, "TRANSACTION_RESTORE");
        std::vector<std::string> parts, status;
        bool contradiction = false;
        if (begin) e.supporting.push_back(ref(*begin, "result", to_string(begin->result)));
        if (!apply) {
            parts.push_back("apply not recorded");
            e.limitations.push_back("TRANSACTION_APPLY not recorded");
        } else if (apply->result == Result::Ok) {
            parts.push_back("applied");
            status.push_back("applied");
            e.supporting.push_back(ref(*apply, "result", "ok"));
        } else {
            parts.push_back("apply failed");
            status.push_back("apply failed");
            e.supporting.push_back(ref(*apply, "result", to_string(apply->result)));
        }
        if (verify) {
            if (verify->result == Result::Ok) {
                parts.push_back("verified by read-back");
                status.push_back("verified");
                e.supporting.push_back(ref(*verify, "result", "ok"));
            } else {
                parts.push_back("verification failed");
                status.push_back("verification failed");
                if (apply && apply->result == Result::Ok) {
                    contradiction = true; // applied, but the read-back disagreed
                    e.contradicting.push_back(ref(*verify, "result", to_string(verify->result)));
                } else {
                    e.supporting.push_back(ref(*verify, "result", to_string(verify->result)));
                }
            }
        } else if (apply && apply->result == Result::Ok) {
            parts.push_back("verification not recorded");
            e.limitations.push_back("TRANSACTION_VERIFY not recorded");
        }
        if (rollback) {
            parts.push_back("rollback " + restore_words(*rollback));
            status.push_back("rolled back");
            e.supporting.push_back(ref(*rollback, "result", to_string(rollback->result)));
            restores.push_back("rollback " + restore_words(*rollback));
        }
        if (restore) {
            parts.push_back("restore " + restore_words(*restore));
            status.push_back(restore->result == Result::Ok ? "restored" : "restore " + std::string(to_string(restore->result)));
            (restore->result == Result::Ok ? e.supporting : e.contradicting)
                .push_back(ref(*restore, "after.not_restored", get(restore->after, "not_restored")));
            restores.push_back(restore_words(*restore));
            if (restore->result != Result::Ok) contradiction = true;
        } else {
            e.limitations.push_back("TRANSACTION_RESTORE not recorded (session may still be active or ended abruptly)");
        }
        if (profile) e.supporting.push_back(ref(*profile, "type", "PROFILE_APPLIED"));
        e.summary = "Transaction " + tx + (subject.empty() ? "" : " (" + subject + ")") + ": " + [&] {
            std::string j;
            for (const auto &p : parts) j += (j.empty() ? "" : "; ") + p;
            return j;
        }() + ".";
        e.primary_finding = status.empty() ? "unknown" : status.back();
        const bool full = apply && (verify || rollback) && (restore || rollback);
        e.confidence = contradiction ? Confidence::Low : full ? Confidence::High : Confidence::Medium;
        std::string st;
        for (const auto &p : status) st += (st.empty() ? "" : ", ") + p;
        statuses.push_back(st.empty() ? "unknown" : st);
        a.explanations.push_back(e);
    }
    if (!statuses.empty()) {
        s.transaction_status = std::to_string(statuses.size()) + " transaction(s): ";
        for (size_t i = 0; i < statuses.size(); ++i) s.transaction_status += (i ? "; " : "") + statuses[i];
    }
    if (!restores.empty()) {
        s.restore_status.clear();
        for (const auto &r : restores) s.restore_status += (s.restore_status.empty() ? "" : "; ") + r;
    }

    // -- recovery (context) ---------------------------------------------------------------------
    if (!recovery.empty()) {
        Explanation e;
        e.topic = "recovery";
        e.related_session_id = session_id;
        e.time_range = {recovery.front()->timestamp_ms, recovery.back()->timestamp_ms};
        bool failed_rec = false, ok_rec = false;
        for (const auto *r : recovery) {
            e.supporting.push_back(ref(*r, "type", r->type));
            failed_rec = failed_rec || r->type == "RECOVERY_FAILED";
            ok_rec = ok_rec || r->type == "RECOVERY_SUCCESS";
        }
        e.summary = std::string("Journal recovery ran during the session window: ") +
                    (failed_rec ? "recovery failed (journal kept)" : ok_rec ? "recovery succeeded" : "outcome not recorded") + ".";
        e.primary_finding = failed_rec ? "recovery failed" : ok_rec ? "recovery succeeded" : "unknown";
        e.confidence = failed_rec || ok_rec ? Confidence::Medium : Confidence::Low;
        e.limitations.push_back("recovery events carry no session_id; associated with this session by time window only");
        a.explanations.push_back(e);
    }

    // -- bottleneck -----------------------------------------------------------------------------
    {
        Explanation e;
        e.topic = "bottleneck";
        e.related_session_id = session_id;
        e.time_range = window;
        if (assessed) {
            const auto primary = get(assessed->after, "primary"), rating = get(assessed->after, "rating");
            const auto conf = get(assessed->after, "confidence");
            const bool conflict = get(assessed->after, "conflict") == "true";
            s.primary_bottleneck = primary.empty() ? "unknown" : primary;
            s.bottleneck_rating = rating.empty() ? "unknown" : rating;
            s.bottleneck_confidence = conf.empty() ? "unknown" : conf;
            e.confidence = parse_confidence(conf).value_or(Confidence::Unknown);
            e.primary_finding = primary == "unknown" || primary.empty()
                                    ? std::string("unknown (") + (conflict ? "conflicting evidence" : "insufficient evidence") + ")"
                                    : primary + " " + rating;
            for (const auto &k : {"primary", "rating", "confidence", "conflict", "samples"})
                e.supporting.push_back(ref(*assessed, std::string("after.") + k, get(assessed->after, k)));
            if (primary != "unknown")
                for (const auto &[k, v] : evidence_of(*assessed, primary)) e.supporting.push_back(ref(*assessed, "after." + k, v));
            const auto secondary = secondary_of(*assessed);
            if (conflict)
                for (const auto &[kind, r] : secondary)
                    for (const auto &[k, v] : evidence_of(*assessed, kind)) e.contradicting.push_back(ref(*assessed, "after." + k, v));
            std::string sec;
            for (const auto &[kind, r] : secondary) sec += (sec.empty() ? "" : ", ") + kind + " " + r;
            e.summary = "The session's recorded bottleneck assessment rates " + e.primary_finding + " (confidence " +
                        (conf.empty() ? "unknown" : conf) + ", " + get(assessed->after, "samples") + " samples)" +
                        (sec.empty() ? "" : "; secondary findings: " + sec) + ". The result is reported as recorded, not re-rated.";
            if (conflict) e.limitations.push_back("conflict: competing categories were rated at least likely; no primary is claimed");
            if (auto note = get(assessed->after, "note"); !note.empty()) e.limitations.push_back("assessment note: " + note);
            if (failed) e.limitations.push_back("a BOTTLENECK_ANALYSIS_FAILED event is also recorded");
        } else if (failed) {
            e.summary = "Bottleneck analysis failed for this session: " + failed->reason;
            e.primary_finding = "unknown (analysis failed)";
            e.supporting.push_back(ref(*failed, "reason", failed->reason));
            e.limitations.push_back("BOTTLENECK_ASSESSED not recorded (analysis failed)");
        } else {
            e.summary = "No bottleneck assessment is recorded for this session.";
            e.primary_finding = "unknown";
            e.limitations.push_back("BOTTLENECK_ASSESSED not recorded");
        }
        if (!assessed) s.limitations.push_back("BOTTLENECK_ASSESSED not recorded; bottleneck, thermal and FPS facts unknown");
        a.explanations.push_back(e);
    }

    // -- thermal ----------------------------------------------------------------------------------
    {
        Explanation e;
        e.topic = "thermal";
        e.related_session_id = session_id;
        e.time_range = window;
        e.primary_finding = "unknown";
        const auto thermal = assessed ? evidence_of(*assessed, "thermal") : decltype(evidence_of(Event{}, "")){};
        if (!thermal.empty()) {
            std::string rating = get(assessed->after, "primary") == "thermal" ? get(assessed->after, "rating") : "";
            std::string others;
            for (const auto &[kind, r] : secondary_of(*assessed)) {
                if (kind == "thermal") rating = r;
                else if (rank(r) >= 1) others += (others.empty() ? "" : ", ") + kind + " " + r;
            }
            const auto p = parse_evidence(thermal.front().second);
            const std::string source = p.ok && !p.source.empty() ? p.source : "unknown source";
            for (const auto &[k, v] : thermal) e.supporting.push_back(ref(*assessed, "after." + k, v));
            e.primary_finding = "thermal constraint reported by " + source + " (" + (rating.empty() ? "unrated" : rating) + ")";
            e.confidence = from_rating(rating);
            e.summary = "Thermal constraint evidence from " + source + " (" + (p.ok ? p.value : thermal.front().second) +
                        ") is correlated with " + (others.empty() ? "no other recorded finding" : others) + " in the same assessment, " +
                        std::to_string(t.transaction_ids.size()) +
                        " transaction(s) in the session. This is a correlation of recorded evidence, not a determination of cause.";
            s.thermal = "thermal constraint evidence (" + (rating.empty() ? "unrated" : rating) + ", " + source + ")";
        } else {
            e.summary = "No thermal constraint evidence is recorded in the bottleneck assessment; thermal state is not "
                        "inferred from temperature.";
            e.limitations.push_back(assessed ? "thermal context may have been missing, stale, unverified, or not reporting a "
                                               "constraint; the recorded assessment does not distinguish these"
                                             : "no bottleneck assessment recorded");
        }
        e.limitations.push_back(kTransitions);
        a.explanations.push_back(e);
    }

    // -- FPS --------------------------------------------------------------------------------------
    {
        auto &f = s.fps;
        Explanation e;
        e.topic = "fps";
        e.related_session_id = session_id;
        e.time_range = window;
        if (assessed) {
            f.source = assessed->event_id;
            for (const auto &[k, v] : assessed->after) {
                if (k.rfind("evidence.", 0) != 0) continue;
                const auto p = parse_evidence(v);
                if (!p.ok) continue;
                if (p.metric == "frame_deficit" && !f.shortfall_samples) {
                    int a1 = 0, b1 = 0;
                    if (std::sscanf(p.value.c_str(), "%d/%d", &a1, &b1) == 2) {
                        f.shortfall_samples = a1;
                        f.fps_samples = b1;
                        e.supporting.push_back(ref(*assessed, "after." + k, v));
                    }
                } else if (p.metric == "fps_vs_refresh" && !f.observed_fps_peak) {
                    double peak = 0;
                    if (std::sscanf(p.value.c_str(), "peak %lf", &peak) == 1) {
                        f.observed_fps_peak = peak;
                        e.supporting.push_back(ref(*assessed, "after." + k, v));
                    }
                } else if (p.metric == "display.refresh.current_hz" && !f.target_refresh_hz) {
                    f.target_refresh_hz = to_double(p.value);
                    e.supporting.push_back(ref(*assessed, "after." + k, v));
                } else if (p.metric == "display.refresh.max_hz" && !f.refresh_capability_hz) {
                    f.refresh_capability_hz = to_double(p.value);
                    e.supporting.push_back(ref(*assessed, "after." + k, v));
                }
            }
        }
        if (!f.target_refresh_hz) f.limitations.push_back("target refresh not recorded in the assessment evidence");
        if (!f.refresh_capability_hz) f.limitations.push_back("refresh capability not recorded in the assessment evidence");
        if (!f.observed_fps_peak) f.limitations.push_back("observed FPS peak not recorded in the assessment evidence");
        if (!f.shortfall_samples) f.limitations.push_back("FPS shortfall not recorded (no FPS with a target in the samples)");
        f.limitations.push_back("per-sample FPS is not persisted; values come from the bottleneck assessment's evidence");
        auto opt = [](const std::optional<double> &v, const char *unit) {
            return v ? num(*v) + " " + unit : std::string("unknown");
        };
        e.summary = "Target refresh " + opt(f.target_refresh_hz, "Hz") + ", refresh capability " +
                    opt(f.refresh_capability_hz, "Hz") + ", observed peak " + opt(f.observed_fps_peak, "FPS") + ", shortfall " +
                    (f.shortfall_samples ? std::to_string(*f.shortfall_samples) + "/" + std::to_string(*f.fps_samples) +
                                               " samples below target"
                                         : std::string("unknown")) +
                    ". Display refresh is not frame rate.";
        e.primary_finding = f.shortfall_samples ? (*f.shortfall_samples > 0 ? "FPS shortfall observed" : "no FPS shortfall observed")
                                                : "unknown";
        e.confidence = f.shortfall_samples ? Confidence::Medium : Confidence::Unknown;
        e.limitations = f.limitations;
        a.explanations.push_back(e);
    }

    // -- completeness ------------------------------------------------------------------------------
    std::vector<std::string> missing;
    if (!t.has_start) missing.push_back("SESSION_START");
    if (!t.has_end) missing.push_back("SESSION_END");
    if (own.empty()) s.completeness = "incomplete: no events recorded";
    else if (missing.empty()) s.completeness = std::string("complete") + (assessed ? "" : " (no bottleneck assessment)");
    else {
        s.completeness = "incomplete: missing";
        for (const auto &m : missing) s.completeness += " " + m;
    }
    if (a.corrupted_lines)
        s.limitations.push_back(std::to_string(a.corrupted_lines) +
                                " corrupt telemetry line(s) skipped in the scanned segments (files not modified)");
    return a;
}

HistoryReport ObservatoryAnalyzer::history(const std::string &package, int64_t from_ms, int64_t to_ms) const {
    HistoryReport h;
    h.package = package;
    if (to_ms - from_ms > kRetentionMs) {
        from_ms = to_ms - kRetentionMs;
        h.limitations.push_back("range clamped to the 7 x 24 h retention window ending at " + std::to_string(to_ms));
    }
    h.range = {from_ms, to_ms};
    TelemetryQuery q;
    q.package = package;
    q.type = "SESSION_START";
    q.from_ms = from_ms;
    q.to_ms = to_ms;
    q.limit = kMaxHistorySessions;
    QueryStats st;
    const auto starts = store_.query(q, &st);
    h.corrupted_lines += st.corrupted_lines;
    if (starts.size() >= kMaxHistorySessions)
        h.limitations.push_back("session limit reached (" + std::to_string(kMaxHistorySessions) + "); later sessions not analysed");

    struct Acc {
        int count = 0;
        std::optional<int64_t> from, to;
        std::vector<std::string> ids;
        void add(const Event &start) {
            ++count;
            if (!from || start.timestamp_ms < *from) from = start.timestamp_ms;
            if (!to || start.timestamp_ms > *to) to = start.timestamp_ms;
            if (ids.size() < kMaxPatternSessions) ids.push_back(start.session_id);
        }
    };
    std::map<std::string, Acc> acc; // ordered: deterministic
    // One session at a time; only counters are kept.
    for (const auto &start : starts) {
        ++h.sessions;
        TelemetryQuery b;
        b.session_id = start.session_id;
        b.type = "BOTTLENECK_ASSESSED";
        QueryStats sb;
        const auto results = store_.query(b, &sb);
        h.corrupted_lines += sb.corrupted_lines;
        if (!results.empty()) {
            ++h.sessions_with_assessment;
            const auto &r = results.back();
            const auto primary = get(r.after, "primary"), rating = get(r.after, "rating");
            if (rank(rating) >= 2 && (primary == "cpu" || primary == "gpu"))
                acc["repeated_" + primary + "_bottleneck"].add(start);
            bool thermal = primary == "thermal" && rank(rating) >= 2;
            for (const auto &[kind, sr] : secondary_of(r)) thermal = thermal || (kind == "thermal" && rank(sr) >= 2);
            if (thermal) acc["repeated_thermal_constraint"].add(start);
        }
        bool restore_failed = false;
        for (const char *type : {"TRANSACTION_RESTORE", "TRANSACTION_ROLLBACK"}) {
            TelemetryQuery tq;
            tq.session_id = start.session_id;
            tq.type = type;
            QueryStats s2;
            for (const auto &e : store_.query(tq, &s2)) restore_failed = restore_failed || e.result != Result::Ok;
            h.corrupted_lines += s2.corrupted_lines;
        }
        if (restore_failed) acc["repeated_restore_failure"].add(start);
    }
    for (const auto &[name, x] : acc) {
        if (x.count < 2) continue; // a single occurrence is not a pattern
        Pattern p;
        p.name = name;
        p.occurrences = x.count;
        const bool needs_assessment = name != "repeated_restore_failure";
        p.sessions_considered = needs_assessment ? h.sessions_with_assessment : h.sessions;
        p.range = {x.from, x.to};
        p.session_ids = x.ids;
        p.confidence = p.sessions_considered >= 3 && x.count * 2 >= p.sessions_considered ? Confidence::Medium : Confidence::Low;
        if (needs_assessment && h.sessions_with_assessment < h.sessions)
            p.limitations.push_back(std::to_string(h.sessions - h.sessions_with_assessment) +
                                    " session(s) without a bottleneck assessment were not compared");
        p.limitations.push_back("a repetition of recorded findings, not a cause; no optimisation or tuning is derived");
        h.patterns.push_back(p);
    }
    if (h.corrupted_lines)
        h.limitations.push_back(std::to_string(h.corrupted_lines) + " corrupt telemetry line(s) skipped (files not modified)");
    return h;
}

std::string to_text(const SessionAnalysis &a) {
    std::ostringstream o;
    o << to_text(a.timeline);
    const auto &s = a.summary;
    o << "summary:\n"
      << "  package: " << (s.package.empty() ? "unknown" : s.package) << "\n"
      << "  duration_ms: " << (s.duration_ms ? std::to_string(*s.duration_ms) : "unknown") << "\n"
      << "  end_reason: " << s.end_reason << "\n"
      << "  target_refresh_hz: " << (s.fps.target_refresh_hz ? num(*s.fps.target_refresh_hz) : "unknown") << "\n"
      << "  refresh_capability_hz: " << (s.fps.refresh_capability_hz ? num(*s.fps.refresh_capability_hz) : "unknown") << "\n"
      << "  observed_fps_peak: " << (s.fps.observed_fps_peak ? num(*s.fps.observed_fps_peak) : "unknown") << "\n"
      << "  fps_shortfall: "
      << (s.fps.shortfall_samples ? std::to_string(*s.fps.shortfall_samples) + "/" + std::to_string(*s.fps.fps_samples) : "unknown")
      << "\n"
      << "  primary_bottleneck: " << s.primary_bottleneck << " (" << s.bottleneck_rating << ", confidence "
      << s.bottleneck_confidence << ")\n"
      << "  thermal: " << s.thermal << "\n"
      << "  transactions: " << s.transaction_status << "\n"
      << "  restore: " << s.restore_status << "\n"
      << "  completeness: " << s.completeness << "\n";
    for (const auto &l : s.limitations) o << "  limitation: " << l << "\n";
    for (const auto &e : a.explanations) o << to_text(e);
    return o.str();
}

std::string to_text(const HistoryReport &h) {
    std::ostringstream o;
    o << "package: " << h.package << "\n"
      << "range_ms: " << (h.range.from_ms ? std::to_string(*h.range.from_ms) : "unknown") << " .. "
      << (h.range.to_ms ? std::to_string(*h.range.to_ms) : "unknown") << "\n"
      << "sessions: " << h.sessions << "\n"
      << "sessions_with_assessment: " << h.sessions_with_assessment << "\n";
    if (h.patterns.empty()) o << "patterns: none\n";
    for (const auto &p : h.patterns) {
        o << "pattern: " << p.name << " occurrences=" << p.occurrences << "/" << p.sessions_considered
          << " confidence=" << to_string(p.confidence) << " range=" << (p.range.from_ms ? std::to_string(*p.range.from_ms) : "?")
          << ".." << (p.range.to_ms ? std::to_string(*p.range.to_ms) : "?") << "\n";
        for (const auto &id : p.session_ids) o << "  session: " << id << "\n";
        for (const auto &l : p.limitations) o << "  limitation: " << l << "\n";
    }
    for (const auto &l : h.limitations) o << "limitation: " << l << "\n";
    return o.str();
}

} // namespace flux::observatory
