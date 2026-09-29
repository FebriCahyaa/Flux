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

// Observatory storage abstraction (docs/architecture/OBSERVATORY.md). Interfaces only plus an
// in-memory reference store; the device telemetry store and its retention come in a later phase.

#include "Event.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace flux::observatory {

struct WriteResult {
    bool accepted = false;
    std::string event_id;
    uint64_t sequence = 0;
    std::vector<std::string> errors; ///< validation errors when rejected
};

struct EventQuery {
    std::optional<std::string> session_id;
    std::optional<Category> category;
    std::optional<std::string> type;
    std::optional<std::string> source;
    std::optional<std::string> transaction_id;
    std::optional<Severity> min_severity;
    std::optional<int64_t> from_ms, to_ms; ///< inclusive
    size_t limit = 0;                      ///< 0 = no limit; applied after ordering (oldest first)
};

/// Producer side: the only thing an engine component needs.
class EventSink {
public:
    virtual ~EventSink() = default;
    /// Validates, assigns event_id (when empty) and sequence, stores. Rejected events are not stored.
    virtual WriteResult write(Event e) = 0;
};

/// Consumer side: timeline, diagnostics, future UI.
class EventSource {
public:
    virtual ~EventSource() = default;
    /// Events in order: timestamp, then sequence (write order) for equal timestamps.
    virtual std::vector<Event> query(const EventQuery &q) const = 0;
};

class EventStore : public EventSink, public EventSource {};

/// Bounded in-memory store: the reference implementation of the ordering and query contract.
class MemoryEventStore final : public EventStore {
public:
    /// @param now_ms  clock for timestamp validation; @param capacity oldest events drop first.
    MemoryEventStore(EventRegistry registry, std::function<int64_t()> now_ms, size_t capacity = 1024);

    WriteResult write(Event e) override;
    std::vector<Event> query(const EventQuery &q) const override;

    size_t size() const { return events_.size(); }
    size_t rejected() const { return rejected_; }
    size_t dropped() const { return dropped_; }

private:
    EventRegistry registry_;
    std::function<int64_t()> now_ms_;
    size_t capacity_;
    std::deque<Event> events_; ///< kept sorted by (timestamp, sequence)
    uint64_t next_seq_ = 1;
    size_t rejected_ = 0, dropped_ = 0;
};

} // namespace flux::observatory
