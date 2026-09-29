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

#include "GamePerformanceRuntime.hpp"

namespace flux::perf {

using flux::runtime::Transaction;
using flux::runtime::TxState;

const char *to_string(RuntimeState s) {
    switch (s) {
    case RuntimeState::Idle: return "idle";
    case RuntimeState::ResolveFailed: return "resolve_failed";
    case RuntimeState::Failed: return "failed";
    case RuntimeState::Active: return "active";
    }
    return "idle";
}

const char *to_string(EndReason r) {
    switch (r) {
    case EndReason::Exit: return "exit";
    case EndReason::ProcessDeath: return "process_death";
    case EndReason::Switch: return "switch";
    case EndReason::Failure: return "failure";
    case EndReason::DaemonStop: return "daemon_stop";
    }
    return "exit";
}

GamePerformanceRuntime::GamePerformanceRuntime(RuntimeDeps deps)
    : d_(std::move(deps)), planner_(d_.node_io, d_.caps, d_.mitigation_allows) {}

std::vector<RecoveryResult> GamePerformanceRuntime::recover() {
    std::vector<RecoveryResult> out;
    for (const std::string &path : {d_.paths.perf_journal, d_.paths.launch_journal, d_.paths.legacy_journal}) {
        if (path.empty() || !d_.files.read) continue;
        const auto text = d_.files.read(path);
        if (!text) continue;
        RecoveryResult r;
        r.journal = path;
        r.report = flux::runtime::recover(d_.node_io, *text);
        if (r.report.clean() && d_.files.remove) r.removed = d_.files.remove(path);
        log("recovery " + path + ": restored " + std::to_string(r.report.restored) + "/" +
            std::to_string(r.report.found) + ", failed " + std::to_string(r.report.failed.size()) +
            ", corrupted " + std::to_string(r.report.corrupted.size()) + (r.removed ? ", journal removed" : ", journal kept"));
        out.push_back(std::move(r));
    }
    return out;
}

std::string GamePerformanceRuntime::next_tx_id(int64_t now_ms) {
    return flux::runtime::make_transaction_id(now_ms, ++seq_);
}

Transaction::JournalSink GamePerformanceRuntime::sink_for(const std::string &path) {
    if (path.empty() || !d_.files.write_atomic) return nullptr;
    return [this, path](const std::string &text) {
        // An empty journal (header only) after a clean restore is removed rather than kept.
        if (flux::runtime::journal::parse(text).entries.empty() && d_.files.remove) {
            d_.files.remove(path);
            return true;
        }
        return d_.files.write_atomic(path, text);
    };
}

bool GamePerformanceRuntime::load_profiles(ProfileDocument &doc) {
    auto read_into = [&](const std::string &path, ProfileDocument &into) {
        if (path.empty() || !d_.files.read) return true;
        const auto text = d_.files.read(path);
        if (!text) return true; // missing file: builtin + global only
        const LoadResult lr = load_document(*text, into);
        for (const auto &w : lr.warnings) warnings_.push_back(path + ": " + w);
        if (!lr.ok) {
            for (const auto &e : lr.errors) error_ += (error_.empty() ? "" : "; ") + path + ": " + e;
            return false;
        }
        return true;
    };
    ProfileDocument library;
    const bool ok_games = read_into(d_.paths.game_profiles, doc);
    const bool ok_lib = read_into(d_.paths.library, library);
    merge_document(doc, library);
    return ok_games && ok_lib;
}

bool GamePerformanceRuntime::on_game_start(const std::string &package, int pid, int64_t now_ms) {
    const bool has_context = state_ != RuntimeState::Idle || tx_ || boost_ || !package_.empty();
    if (has_context && package == package_ && pid == pid_) return false;
    if (has_context) on_game_end(EndReason::Switch);

    package_ = package;
    pid_ = pid;
    warnings_.clear();
    error_.clear();
    refresh_target_hz_ = 0;

    ProfileDocument doc;
    if (!load_profiles(doc)) {
        state_ = RuntimeState::ResolveFailed;
        log("game " + package + ": profiles unreadable, nothing applied: " + error_);
        return true;
    }
    if (!doc.games.count(package) && d_.gamelist_lite) {
        if (const auto lite = d_.gamelist_lite(package)) doc.games[package] = layer_from_gamelist(*lite);
    }
    profile_ = resolve(doc, package);
    for (const auto &w : warnings_) log("game " + package + ": " + w);
    if (!profile_.ok()) {
        for (const auto &e : profile_.errors) error_ += (error_.empty() ? "" : "; ") + e;
        state_ = RuntimeState::ResolveFailed;
        log("game " + package + ": profile resolve failed, nothing applied: " + error_);
        return true;
    }

    PerformancePlanResult planned = planner_.plan(profile_.perf, {package});
    if (!planned.errors.empty()) {
        for (const auto &e : planned.errors) error_ += (error_.empty() ? "" : "; ") + e;
        state_ = RuntimeState::ResolveFailed;
        log("game " + package + ": plan rejected, nothing applied: " + error_);
        return true;
    }

    if (!planned.plan.operations.empty()) {
        tx_ = std::make_unique<Transaction>(next_tx_id(now_ms), std::move(planned.plan), sink_for(d_.paths.perf_journal));
        if (!tx_->start()) {
            error_ = "transaction " + tx_->id() + " failed and was rolled back";
            for (const auto &l : tx_->log()) log("game " + package + ": " + l);
            log("game " + package + ": " + error_ + "; Flux profile unaffected");
            tx_.reset();
            state_ = RuntimeState::Failed;
            return true;
        }
        log("game " + package + ": transaction " + tx_->id() + " " + flux::runtime::to_string(tx_->state()));
    }

    if (profile_.perf.launch_boost) {
        boost_ = std::make_unique<LaunchBoost>(planner_);
        if (!boost_->begin({package}, now_ms, next_tx_id(now_ms), sink_for(d_.paths.launch_journal))) {
            log("game " + package + ": launch boost not started");
            boost_.reset();
        }
    }

    refresh_target_hz_ = planned.refresh_target_hz;
    const bool applied = (tx_ && tx_->state() == TxState::Active) || launch_boost_active();
    state_ = applied || refresh_target_hz_ > 0 ? RuntimeState::Active : RuntimeState::Idle;
    return true;
}

void GamePerformanceRuntime::tick(int64_t now_ms) {
    if (boost_ && !boost_->tick(now_ms) && boost_->phase() == LaunchBoost::Phase::Done) {
        log("game " + package_ + ": launch boost ended (" + boost_->last_cancel() + ")" +
            (boost_->restored_clean() ? "" : ", restore incomplete"));
        boost_.reset();
    }
}

bool GamePerformanceRuntime::after_profile_script() {
    return tx_ ? tx_->reapply() : true;
}

bool GamePerformanceRuntime::on_game_end(EndReason why) {
    bool clean = true;
    if (boost_) {
        clean = boost_->cancel(why == EndReason::ProcessDeath ? LaunchBoost::Cancel::ProcessExit
                                                              : LaunchBoost::Cancel::ProfileChange) && clean;
        boost_.reset();
    }
    if (tx_) {
        const bool ok = tx_->finish();
        if (!ok) log("game " + package_ + ": restore incomplete, journal kept for recovery");
        clean = ok && clean;
        tx_.reset();
    }
    if (!package_.empty()) log("game " + package_ + ": ended (" + to_string(why) + ")" + (clean ? "" : ", not clean"));
    package_.clear();
    pid_ = 0;
    refresh_target_hz_ = 0;
    state_ = RuntimeState::Idle;
    return clean;
}

} // namespace flux::perf
