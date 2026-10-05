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

#pragma once

// Zairenkai Observatory event model (docs/architecture/EVENT_MODEL.md).
//
// An event records one thing that happened, with the evidence for it: who produced it, why, the
// state before and after, how sure the producer is, and how it ended. Producers only report;
// the Observatory validates, orders and stores. Nothing here makes a policy decision.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace flux::observatory {

inline constexpr int kSchemaVersion = 1;

enum class Category { Session, Runtime, Performance, Transaction, Recovery, Observatory };
enum class Severity { Debug, Info, Notice, Warning, Error, Critical };
enum class Confidence { High, Medium, Low, Unknown };
enum class Result { Ok, Failed, Partial, Skipped, Unknown };

const char *to_string(Category c);
const char *to_string(Severity s);
const char *to_string(Confidence c);
const char *to_string(Result r);
std::optional<Category> parse_category(const std::string &s);
std::optional<Severity> parse_severity(const std::string &s);
std::optional<Confidence> parse_confidence(const std::string &s);
std::optional<Result> parse_result(const std::string &s);

/// before / after: small key -> value snapshots ("swappiness" -> "100"). Empty when not relevant.
using StateSnapshot = std::map<std::string, std::string>;

struct Event {
    std::string event_id;          ///< assigned by the store when empty
    int64_t timestamp_ms = 0;      ///< wall clock, Unix epoch milliseconds
    std::string source;            ///< producing component, e.g. "session", "game_runtime"
    std::string type;              ///< registered type, e.g. "SESSION_START"
    Severity severity = Severity::Info;
    std::string session_id;        ///< empty outside a session
    std::string reason;            ///< why it happened; "reason unavailable" when unknown, never invented
    StateSnapshot before, after;
    Confidence confidence = Confidence::Unknown;
    Result result = Result::Unknown;
    std::string transaction_id;    ///< required by TRANSACTION events
    uint64_t sequence = 0;         ///< store-assigned total order, 0 before storing
};

// -- registry ---------------------------------------------------------------------------------------

struct EventType {
    std::string name;
    Category category;
    std::vector<std::string> sources;   ///< producers allowed to emit it
    bool requires_session = false;
    bool requires_transaction = false;
    std::string description;
};

class EventRegistry {
public:
    /// The initial registry: SESSION, RUNTIME, PERFORMANCE, TRANSACTION, RECOVERY types.
    static EventRegistry builtin();

    /// False when the name is already registered or malformed (UPPER_SNAKE_CASE, 3–48 chars).
    bool add(EventType t);
    const EventType *find(const std::string &name) const;
    const std::map<std::string, EventType> &types() const { return types_; }

private:
    std::map<std::string, EventType> types_;
};

// -- validation ---------------------------------------------------------------------------------

struct ValidationPolicy {
    int64_t min_timestamp_ms = 1577836800000;  ///< 2020-01-01: anything earlier is a broken clock
    int64_t max_future_skew_ms = 24LL * 3600 * 1000;
};

/// Empty when valid. `now_ms` bounds timestamps from above.
std::vector<std::string> validate(const Event &e, const EventRegistry &reg, int64_t now_ms,
                                  const ValidationPolicy &policy = {});

// -- serialisation ------------------------------------------------------------------------------

/// One JSON object, no newline (JSONL line).
std::string to_json(const Event &e);

/// Strict parse of one JSON object. Unknown keys are rejected, so a corrupted or foreign line is
/// never half-read. Semantic validation is separate (validate()).
bool from_json(const std::string &line, Event &out, std::string &error);

struct ParsedLines {
    std::vector<Event> events;
    std::vector<std::string> corrupted; ///< "line N: <error>"
};
/// Parse a JSONL text; blank lines ignored, bad lines reported and skipped.
ParsedLines parse_jsonl(const std::string &text);

} // namespace flux::observatory
