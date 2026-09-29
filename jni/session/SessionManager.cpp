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

#include "SessionManager.hpp"

namespace flux::session {

namespace {
constexpr size_t kMaxEvents = 64;
}

const char *to_string(EndReason r) {
    switch (r) {
    case EndReason::Exit: return "exit";
    case EndReason::FocusLost: return "focus_lost";
    case EndReason::ProcessDeath: return "process_death";
    case EndReason::Switch: return "switch";
    case EndReason::Failure: return "failure";
    case EndReason::DaemonStop: return "daemon_stop";
    }
    return "exit";
}

void SessionManager::note(const std::string &e) {
    if (events_.size() >= kMaxEvents) events_.erase(events_.begin());
    events_.push_back(e);
}

void SessionManager::recover() {
    if (recovered_) return;
    recovered_ = true;
    for (auto *p : participants_) p->recover();
    note("recover");
}

bool SessionManager::begin(const SessionKey &key, int64_t now_ms) {
    recover(); // never begin before what a previous daemon left behind is undone
    if (current_.active && current_.key.same_process(key)) return false;
    if (current_.active) end(EndReason::Switch, now_ms);

    current_ = SessionInfo{};
    current_.id = "s-" + std::to_string(now_ms) + "-" + std::to_string(++seq_);
    current_.key = key;
    current_.started_ms = now_ms;
    current_.active = true;
    for (auto *p : participants_) p->begin(current_);
    note("begin " + current_.id + " " + key.package + " pid " + std::to_string(key.pid));
    return true;
}

bool SessionManager::end(EndReason why, int64_t now_ms) {
    if (!current_.active) return false;
    current_.end_reason = why;
    current_.ended_ms = now_ms;
    bool clean = true;
    // Reverse of begin order: observers finish before the runtime restores what it changed.
    for (auto it = participants_.rbegin(); it != participants_.rend(); ++it)
        if (!(*it)->end(current_, why)) clean = false;
    current_.active = false;
    note("end " + current_.id + " " + to_string(why) + (clean ? "" : " (not clean)"));
    last_ = current_;
    current_ = SessionInfo{};
    return clean;
}

void SessionManager::profile_applied() {
    if (!current_.active) return;
    for (auto *p : participants_) p->profile_applied(current_);
}

void SessionManager::tick(int64_t now_ms) {
    if (!current_.active) return;
    for (auto *p : participants_) p->tick(current_, now_ms);
}

bool SessionManager::needs_tick() const {
    if (!current_.active) return false;
    for (const auto *p : participants_)
        if (p->needs_tick()) return true;
    return false;
}

} // namespace flux::session
