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

// Observatory event integration bridge (docs/architecture/OBSERVATORY.md).
//
// Session, GameRuntime and the Transaction Engine know nothing about the Observatory: they call
// optional neutral observers after each successful transition. This bridge turns those notices
// into schema-v1 events and writes them to an EventSink. A missing sink, a rejected event or a
// sink that throws is counted here and never reaches the producer.

#include "EventStore.hpp"
#include "GamePerformanceRuntime.hpp"
#include "SessionManager.hpp"
#include "Transaction.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace flux::bridge {

class ObservatoryBridge {
public:
    /// @param sink may be null (Observatory unavailable). @param wall_ms Unix epoch milliseconds.
    ObservatoryBridge(flux::observatory::EventSink *sink, std::function<int64_t()> wall_ms);

    void set_sink(flux::observatory::EventSink *sink) { sink_ = sink; }

    /// Observers to hand to the producers.
    std::function<void(const flux::session::SessionNotice &)> session_observer();
    std::function<void(const std::string &)> session_context();
    std::function<void(const flux::perf::RuntimeNotice &)> runtime_observer();
    flux::runtime::TxObserver transaction_observer();

    struct Stats {
        size_t emitted = 0;  ///< accepted by the sink
        size_t rejected = 0; ///< refused by validation
        size_t failed = 0;   ///< sink threw or bridge could not build the event
        size_t dropped = 0;  ///< no sink
    };
    const Stats &stats() const { return stats_; }
    const std::string &session_id() const { return session_id_; }

    void on_session(const flux::session::SessionNotice &n);
    void on_runtime(const flux::perf::RuntimeNotice &n);
    void on_transaction(const flux::runtime::TxNotice &n);

private:
    void emit(flux::observatory::Event e);

    flux::observatory::EventSink *sink_;
    std::function<int64_t()> wall_ms_;
    std::string session_id_;
    Stats stats_;
};

} // namespace flux::bridge
