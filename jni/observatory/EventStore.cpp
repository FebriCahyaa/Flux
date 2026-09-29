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

#include "EventStore.hpp"

#include <algorithm>

namespace flux::observatory {

MemoryEventStore::MemoryEventStore(EventRegistry registry, std::function<int64_t()> now_ms, size_t capacity)
    : registry_(std::move(registry)), now_ms_(std::move(now_ms)), capacity_(std::max<size_t>(capacity, 1)) {}

WriteResult MemoryEventStore::write(Event e) {
    WriteResult r;
    r.errors = validate(e, registry_, now_ms_ ? now_ms_() : 0);
    if (!r.errors.empty()) {
        ++rejected_;
        return r;
    }
    e.sequence = next_seq_++;
    if (e.event_id.empty()) e.event_id = "ev-" + std::to_string(e.timestamp_ms) + "-" + std::to_string(e.sequence);
    // Keep (timestamp, sequence) order: a later write with an equal timestamp goes after the existing ones.
    auto pos = std::upper_bound(events_.begin(), events_.end(), e.timestamp_ms,
                                [](int64_t ts, const Event &x) { return ts < x.timestamp_ms; });
    r.accepted = true;
    r.event_id = e.event_id;
    r.sequence = e.sequence;
    events_.insert(pos, std::move(e));
    while (events_.size() > capacity_) {
        events_.pop_front();
        ++dropped_;
    }
    return r;
}

std::vector<Event> MemoryEventStore::query(const EventQuery &q) const {
    std::vector<Event> out;
    for (const Event &e : events_) {
        if (q.session_id && e.session_id != *q.session_id) continue;
        if (q.type && e.type != *q.type) continue;
        if (q.source && e.source != *q.source) continue;
        if (q.transaction_id && e.transaction_id != *q.transaction_id) continue;
        if (q.min_severity && e.severity < *q.min_severity) continue;
        if (q.from_ms && e.timestamp_ms < *q.from_ms) continue;
        if (q.to_ms && e.timestamp_ms > *q.to_ms) continue;
        if (q.category) {
            const EventType *t = registry_.find(e.type);
            if (!t || t->category != *q.category) continue;
        }
        out.push_back(e);
        if (q.limit && out.size() == q.limit) break;
    }
    return out;
}

} // namespace flux::observatory
