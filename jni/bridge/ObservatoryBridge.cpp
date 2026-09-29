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

#include "ObservatoryBridge.hpp"

namespace flux::bridge {

using flux::observatory::Confidence;
using flux::observatory::Event;
using flux::observatory::Result;
using flux::observatory::Severity;
using flux::observatory::StateSnapshot;

namespace {

constexpr size_t kMaxKeys = 64, kMaxKey = 128, kMaxValue = 512, kMaxReason = 512;

std::string clip(const std::string &s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }

/// Evidence maps can be large (many nodes); keep them inside the schema limits rather than lose the event.
StateSnapshot bounded(const std::map<std::string, std::string> &m) {
    StateSnapshot out;
    for (const auto &[k, v] : m) {
        if (out.size() == kMaxKeys) break;
        if (k.empty()) continue;
        out[clip(k, kMaxKey)] = clip(v, kMaxValue);
    }
    return out;
}

std::string reason_of(const std::string &detail) {
    return detail.empty() ? std::string("reason unavailable") : clip(detail, kMaxReason);
}

} // namespace

ObservatoryBridge::ObservatoryBridge(flux::observatory::EventSink *sink, std::function<int64_t()> wall_ms)
    : sink_(sink), wall_ms_(std::move(wall_ms)) {}

void ObservatoryBridge::emit(Event e) {
    if (!sink_) {
        ++stats_.dropped;
        return;
    }
    try {
        e.timestamp_ms = wall_ms_ ? wall_ms_() : 0;
        if (sink_->write(std::move(e)).accepted) ++stats_.emitted;
        else ++stats_.rejected;
    } catch (...) {
        ++stats_.failed; // the Observatory failing is never the producer's problem
    }
}

std::function<void(const flux::session::SessionNotice &)> ObservatoryBridge::session_observer() {
    return [this](const flux::session::SessionNotice &n) { on_session(n); };
}

std::function<void(const std::string &)> ObservatoryBridge::session_context() {
    return [this](const std::string &id) { session_id_ = id; };
}

std::function<void(const flux::perf::RuntimeNotice &)> ObservatoryBridge::runtime_observer() {
    return [this](const flux::perf::RuntimeNotice &n) { on_runtime(n); };
}

flux::runtime::TxObserver ObservatoryBridge::transaction_observer() {
    return [this](const flux::runtime::TxNotice &n) { on_transaction(n); };
}

void ObservatoryBridge::on_session(const flux::session::SessionNotice &n) {
    using K = flux::session::SessionNotice::Kind;
    const auto &s = n.session;
    Event e;
    e.source = "session";
    e.session_id = s.id;
    e.confidence = Confidence::High;
    switch (n.kind) {
    case K::Start:
        e.type = "SESSION_START";
        e.reason = "game " + s.key.package + " in foreground (pid " + std::to_string(s.key.pid) + ")";
        e.after = {{"package", s.key.package}, {"pid", std::to_string(s.key.pid)}, {"uid", std::to_string(s.key.uid)}};
        e.result = Result::Ok;
        break;
    case K::End:
        e.type = "SESSION_END";
        e.reason = std::string("end reason: ") + flux::session::to_string(s.end_reason);
        e.before = {{"package", s.key.package}, {"pid", std::to_string(s.key.pid)}};
        e.after = {{"end_reason", flux::session::to_string(s.end_reason)},
                   {"duration_ms", std::to_string(s.ended_ms - s.started_ms)},
                   {"clean", n.clean ? "true" : "false"}};
        e.result = n.clean ? Result::Ok : Result::Partial;
        e.severity = n.clean ? Severity::Info : Severity::Warning;
        break;
    case K::Switch:
        e.type = "SESSION_SWITCH";
        e.reason = "replaced by " + s.key.package + " pid " + std::to_string(s.key.pid);
        e.before = {{"session_id", n.previous_id}};
        e.after = {{"session_id", s.id}, {"package", s.key.package}};
        e.result = Result::Ok;
        break;
    }
    emit(std::move(e));
}

void ObservatoryBridge::on_runtime(const flux::perf::RuntimeNotice &n) {
    using K = flux::perf::RuntimeNotice::Kind;
    Event e;
    e.session_id = session_id_;
    e.reason = reason_of(n.detail);
    e.before = bounded(n.before);
    e.after = bounded(n.after);
    if (!n.package.empty() && e.after.size() < kMaxKeys) e.after.emplace("package", n.package);
    e.confidence = Confidence::High;
    e.result = n.ok ? Result::Ok : Result::Partial;
    switch (n.kind) {
    case K::Activate: e.type = "RUNTIME_ACTIVATE"; e.source = "game_runtime"; break;
    case K::Restore: e.type = "RUNTIME_RESTORE"; e.source = "game_runtime"; break;
    case K::Failure:
        e.type = "RUNTIME_FAILURE";
        e.source = "game_runtime";
        e.severity = Severity::Warning;
        e.result = Result::Failed;
        break;
    case K::ProfileApplied: e.type = "PROFILE_APPLIED"; e.source = "performance"; break;
    case K::ProfileRestored: e.type = "PROFILE_RESTORED"; e.source = "performance"; break;
    case K::RecoveryStart: e.type = "RECOVERY_START"; e.source = "recovery"; e.session_id.clear(); break;
    case K::RecoverySuccess: e.type = "RECOVERY_SUCCESS"; e.source = "recovery"; e.session_id.clear(); break;
    case K::RecoveryFailed:
        e.type = "RECOVERY_FAILED";
        e.source = "recovery";
        e.session_id.clear();
        e.severity = Severity::Error;
        e.result = Result::Partial;
        break;
    }
    if (!n.ok && e.severity < Severity::Warning) e.severity = Severity::Warning;
    emit(std::move(e));
}

void ObservatoryBridge::on_transaction(const flux::runtime::TxNotice &n) {
    using K = flux::runtime::TxNotice::Kind;
    Event e;
    e.source = "transaction";
    e.session_id = session_id_;
    e.transaction_id = n.tx_id;
    e.reason = reason_of(n.detail);
    e.before = bounded(n.before);
    e.after = bounded(n.after);
    e.confidence = Confidence::High; // every state claim here was read back from the node
    e.result = n.ok ? Result::Ok : Result::Failed;
    e.severity = n.ok ? Severity::Info : Severity::Warning;
    switch (n.kind) {
    case K::Begin: e.type = "TRANSACTION_BEGIN"; e.severity = Severity::Debug; break;
    case K::Apply: e.type = "TRANSACTION_APPLY"; break;
    case K::Verify: e.type = "TRANSACTION_VERIFY"; break;
    case K::Rollback:
        e.type = "TRANSACTION_ROLLBACK";
        e.severity = n.ok ? Severity::Warning : Severity::Error; // a rollback always follows a failure
        e.result = n.ok ? Result::Ok : Result::Partial;
        break;
    case K::Restore:
        e.type = "TRANSACTION_RESTORE";
        e.result = n.ok ? Result::Ok : Result::Partial;
        if (!n.ok) e.severity = Severity::Error;
        break;
    }
    if (e.after.size() < kMaxKeys) e.after.emplace("subject", n.subject);
    emit(std::move(e));
}

} // namespace flux::bridge
