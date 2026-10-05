/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Event.hpp"

#include <algorithm>
#include <set>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::observatory {

namespace {

template <typename E, size_t N>
std::optional<E> parse_enum(const std::string &s, const std::pair<E, const char *> (&table)[N]) {
    for (const auto &[v, name] : table)
        if (s == name) return v;
    return std::nullopt;
}

template <typename E, size_t N>
const char *enum_name(E v, const std::pair<E, const char *> (&table)[N]) {
    for (const auto &[e, name] : table)
        if (e == v) return name;
    return "unknown";
}

const std::pair<Category, const char *> kCategories[] = {{Category::Session, "session"},
                                                         {Category::Runtime, "runtime"},
                                                         {Category::Performance, "performance"},
                                                         {Category::Transaction, "transaction"},
                                                         {Category::Recovery, "recovery"},
                                                         {Category::Observatory, "observatory"}};
const std::pair<Severity, const char *> kSeverities[] = {{Severity::Debug, "debug"},     {Severity::Info, "info"},
                                                         {Severity::Notice, "notice"},   {Severity::Warning, "warning"},
                                                         {Severity::Error, "error"},     {Severity::Critical, "critical"}};
const std::pair<Confidence, const char *> kConfidences[] = {{Confidence::High, "high"},
                                                            {Confidence::Medium, "medium"},
                                                            {Confidence::Low, "low"},
                                                            {Confidence::Unknown, "unknown"}};
const std::pair<Result, const char *> kResults[] = {{Result::Ok, "ok"},           {Result::Failed, "failed"},
                                                    {Result::Partial, "partial"}, {Result::Skipped, "skipped"},
                                                    {Result::Unknown, "unknown"}};

constexpr size_t kMaxSource = 32, kMaxId = 64, kMaxReason = 512, kMaxKeys = 64, kMaxKey = 128, kMaxValue = 512;

bool valid_type_name(const std::string &n) {
    if (n.size() < 3 || n.size() > 48 || !(n[0] >= 'A' && n[0] <= 'Z')) return false;
    return std::all_of(n.begin(), n.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool valid_source(const std::string &s) {
    return !s.empty() && s.size() <= kMaxSource &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

void check_snapshot(const char *which, const StateSnapshot &s, std::vector<std::string> &err) {
    if (s.size() > kMaxKeys) err.push_back(std::string(which) + " has more than 64 keys");
    for (const auto &[k, v] : s)
        if (k.empty() || k.size() > kMaxKey || v.size() > kMaxValue)
            err.push_back(std::string(which) + " has an empty or oversized key/value");
}

} // namespace

const char *to_string(Category c) { return enum_name(c, kCategories); }
const char *to_string(Severity s) { return enum_name(s, kSeverities); }
const char *to_string(Confidence c) { return enum_name(c, kConfidences); }
const char *to_string(Result r) { return enum_name(r, kResults); }
std::optional<Category> parse_category(const std::string &s) { return parse_enum(s, kCategories); }
std::optional<Severity> parse_severity(const std::string &s) { return parse_enum(s, kSeverities); }
std::optional<Confidence> parse_confidence(const std::string &s) { return parse_enum(s, kConfidences); }
std::optional<Result> parse_result(const std::string &s) { return parse_enum(s, kResults); }

// -- registry -------------------------------------------------------------------------------------

bool EventRegistry::add(EventType t) {
    if (!valid_type_name(t.name) || types_.count(t.name) || t.sources.empty()) return false;
    types_.emplace(t.name, std::move(t));
    return true;
}

const EventType *EventRegistry::find(const std::string &name) const {
    auto it = types_.find(name);
    return it == types_.end() ? nullptr : &it->second;
}

EventRegistry EventRegistry::builtin() {
    EventRegistry r;
    auto add = [&](const char *n, Category c, std::vector<std::string> src, bool sess, bool tx, const char *d) {
        r.add({n, c, std::move(src), sess, tx, d});
    };
    const std::vector<std::string> session{"session"}, runtime{"game_runtime"}, perf{"performance"},
        tx{"transaction"}, recovery{"recovery"};

    add("SESSION_START", Category::Session, session, true, false, "a game session began (all participants started)");
    add("SESSION_END", Category::Session, session, true, false, "a game session ended; reason = end reason");
    add("SESSION_SWITCH", Category::Session, session, true, false, "another game or a restarted process replaced the session");

    add("RUNTIME_ACTIVATE", Category::Runtime, runtime, false, false, "per-game performance context became active");
    add("RUNTIME_RESTORE", Category::Runtime, runtime, false, false, "per-game performance context ended and was restored");
    add("RUNTIME_FAILURE", Category::Runtime, runtime, false, false, "profile unreadable/unresolvable or transaction failed; nothing left applied");

    add("PROFILE_APPLIED", Category::Performance, perf, false, false, "per-game values applied and read back");
    add("PROFILE_RESTORED", Category::Performance, perf, false, false, "per-game values restored");

    add("TRANSACTION_BEGIN", Category::Transaction, tx, false, true, "transaction started");
    add("TRANSACTION_APPLY", Category::Transaction, tx, false, true, "operations written (or a write failed)");
    add("TRANSACTION_VERIFY", Category::Transaction, tx, false, true, "read-back verification result");
    add("TRANSACTION_ROLLBACK", Category::Transaction, tx, false, true, "applied operations undone after a failure");
    add("TRANSACTION_RESTORE", Category::Transaction, tx, false, true, "restore at the end of the lifecycle");

    add("RECOVERY_START", Category::Recovery, recovery, false, false, "journal replay started at daemon start");
    add("RECOVERY_SUCCESS", Category::Recovery, recovery, false, false, "journal restored cleanly (or none left behind)");
    add("RECOVERY_FAILED", Category::Recovery, recovery, false, false, "journal kept: failed or corrupted entries");

    const std::vector<std::string> bottleneck{"bottleneck"};
    add("BOTTLENECK_ASSESSED", Category::Performance, bottleneck, true, false,
        "final bottleneck assessment of a session (observation only; evidence in `after`)");
    add("BOTTLENECK_ANALYSIS_FAILED", Category::Performance, bottleneck, true, false,
        "the session's bottleneck analysis could not complete; no finding was claimed");

    add("OBSERVATORY_STORAGE_FAILED", Category::Observatory, {"observatory"}, false, false,
        "persistent telemetry write failed (recorded in memory only); runtime unaffected");
    return r;
}

// -- validation -------------------------------------------------------------------------------------

std::vector<std::string> validate(const Event &e, const EventRegistry &reg, int64_t now_ms,
                                  const ValidationPolicy &policy) {
    std::vector<std::string> err;
    if (e.event_id.size() > kMaxId) err.push_back("event_id longer than 64 characters");
    if (e.timestamp_ms <= 0 || e.timestamp_ms < policy.min_timestamp_ms)
        err.push_back("timestamp missing or before 2020 (clock not set?)");
    else if (e.timestamp_ms > now_ms + policy.max_future_skew_ms)
        err.push_back("timestamp too far in the future");
    if (!valid_source(e.source)) err.push_back("source missing or not lower_snake_case");
    const EventType *t = reg.find(e.type);
    if (!t) {
        err.push_back("unknown event type '" + e.type + "'");
    } else {
        if (valid_source(e.source) && std::find(t->sources.begin(), t->sources.end(), e.source) == t->sources.end())
            err.push_back("source '" + e.source + "' not allowed for " + e.type);
        if (t->requires_session && e.session_id.empty()) err.push_back(e.type + " requires session_id");
        if (t->requires_transaction && e.transaction_id.empty()) err.push_back(e.type + " requires transaction_id");
    }
    if (e.reason.empty()) err.push_back("reason missing (use \"reason unavailable\" when unknown)");
    if (e.reason.size() > kMaxReason) err.push_back("reason longer than 512 characters");
    if (e.session_id.size() > kMaxId || e.transaction_id.size() > kMaxId) err.push_back("session_id/transaction_id too long");
    check_snapshot("before", e.before, err);
    check_snapshot("after", e.after, err);
    return err;
}

// -- serialisation ------------------------------------------------------------------------------------

std::string to_json(const Event &e) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    auto str = [&](const char *k, const std::string &v) {
        w.Key(k);
        w.String(v.c_str(), static_cast<rapidjson::SizeType>(v.size()));
    };
    auto snap = [&](const char *k, const StateSnapshot &s) {
        w.Key(k);
        w.StartObject();
        for (const auto &[a, b] : s) {
            w.Key(a.c_str(), static_cast<rapidjson::SizeType>(a.size()));
            w.String(b.c_str(), static_cast<rapidjson::SizeType>(b.size()));
        }
        w.EndObject();
    };
    w.StartObject();
    w.Key("schema");
    w.Int(kSchemaVersion);
    str("event_id", e.event_id);
    w.Key("sequence");
    w.Uint64(e.sequence);
    w.Key("timestamp_ms");
    w.Int64(e.timestamp_ms);
    str("source", e.source);
    str("type", e.type);
    str("severity", to_string(e.severity));
    str("session_id", e.session_id);
    str("reason", e.reason);
    snap("before", e.before);
    snap("after", e.after);
    str("confidence", to_string(e.confidence));
    str("result", to_string(e.result));
    str("transaction_id", e.transaction_id);
    w.EndObject();
    return sb.GetString();
}

bool from_json(const std::string &line, Event &out, std::string &error) {
    rapidjson::Document d;
    d.Parse(line.c_str(), line.size());
    if (d.HasParseError() || !d.IsObject()) {
        error = "not a JSON object";
        return false;
    }
    static const std::set<std::string> kKeys = {"schema", "event_id", "sequence", "timestamp_ms", "source",
                                                "type", "severity", "session_id", "reason", "before",
                                                "after", "confidence", "result", "transaction_id"};
    for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it)
        if (!kKeys.count(it->name.GetString())) {
            error = std::string("unknown key '") + it->name.GetString() + "'";
            return false;
        }
    for (const auto &k : kKeys)
        if (!d.HasMember(k.c_str())) {
            error = "missing key '" + k + "'";
            return false;
        }
    if (!d["schema"].IsInt() || d["schema"].GetInt() != kSchemaVersion) {
        error = "unsupported schema";
        return false;
    }
    Event e;
    auto get_str = [&](const char *k, std::string &dst) {
        if (!d[k].IsString()) {
            error = std::string(k) + " must be a string";
            return false;
        }
        dst = d[k].GetString();
        return true;
    };
    auto get_snap = [&](const char *k, StateSnapshot &dst) {
        if (!d[k].IsObject()) {
            error = std::string(k) + " must be an object";
            return false;
        }
        for (auto m = d[k].MemberBegin(); m != d[k].MemberEnd(); ++m) {
            if (!m->value.IsString()) {
                error = std::string(k) + " values must be strings";
                return false;
            }
            dst[m->name.GetString()] = m->value.GetString();
        }
        return true;
    };
    std::string severity, confidence, result;
    if (!get_str("event_id", e.event_id) || !get_str("source", e.source) || !get_str("type", e.type) ||
        !get_str("severity", severity) || !get_str("session_id", e.session_id) || !get_str("reason", e.reason) ||
        !get_str("confidence", confidence) || !get_str("result", result) ||
        !get_str("transaction_id", e.transaction_id) || !get_snap("before", e.before) || !get_snap("after", e.after))
        return false;
    if (!d["timestamp_ms"].IsInt64()) {
        error = "timestamp_ms must be an integer";
        return false;
    }
    e.timestamp_ms = d["timestamp_ms"].GetInt64();
    if (!d["sequence"].IsUint64()) {
        error = "sequence must be an unsigned integer";
        return false;
    }
    e.sequence = d["sequence"].GetUint64();
    const auto sv = parse_severity(severity);
    const auto cv = parse_confidence(confidence);
    const auto rv = parse_result(result);
    if (!sv) { error = "invalid severity '" + severity + "'"; return false; }
    if (!cv) { error = "invalid confidence '" + confidence + "'"; return false; }
    if (!rv) { error = "invalid result '" + result + "'"; return false; }
    e.severity = *sv;
    e.confidence = *cv;
    e.result = *rv;
    out = std::move(e);
    return true;
}

ParsedLines parse_jsonl(const std::string &text) {
    ParsedLines out;
    size_t start = 0, n = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            Event e;
            std::string err;
            if (from_json(line, e, err)) out.events.push_back(std::move(e));
            else out.corrupted.push_back("line " + std::to_string(n) + ": " + err);
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return out;
}

} // namespace flux::observatory
