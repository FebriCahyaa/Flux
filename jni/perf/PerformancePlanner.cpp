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

#include "PerformancePlanner.hpp"

#include <algorithm>
#include <array>

namespace flux::perf {

using flux::runtime::NodeWriteOperation;
using flux::runtime::RuntimePlan;

namespace {

constexpr const char *kVm = "/proc/sys/vm/";
constexpr const char *kInputBoost = "/sys/module/cpu_boost/parameters/input_boost_ms";
constexpr const char *kTopAppUclampMin = "/dev/cpuctl/top-app/cpu.uclamp.min";

constexpr std::array<const char *, 6> kVmNodes = {"swappiness", "vfs_cache_pressure", "page-cluster",
                                                  "dirty_expire_centisecs", "dirty_writeback_centisecs",
                                                  "watermark_boost_factor"};

bool starts_with(const std::string &s, const std::string &p) { return s.rfind(p, 0) == 0; }

/// "/sys/block/<dev>/queue" with a single, plain device name.
bool is_block_queue(const std::string &dir) {
    const std::string pre = "/sys/block/", post = "/queue";
    if (!starts_with(dir, pre) || dir.size() <= pre.size() + post.size()) return false;
    if (dir.compare(dir.size() - post.size(), post.size(), post) != 0) return false;
    const std::string dev = dir.substr(pre.size(), dir.size() - pre.size() - post.size());
    return !dev.empty() && dev.find('/') == std::string::npos && dev != "." && dev != "..";
}

std::optional<int> refresh_hz(const std::string &sel, int custom) {
    if (sel == "hz60") return 60;
    if (sel == "hz90") return 90;
    if (sel == "hz120") return 120;
    if (sel == "hz144") return 144;
    if (sel == "custom") return custom;
    return std::nullopt;
}

} // namespace

const char *to_string(Support s) {
    switch (s) {
    case Support::Auto: return "auto";
    case Support::Supported: return "supported";
    case Support::Unsupported: return "unsupported";
    }
    return "auto";
}

bool PerformancePlanner::interface_allowed(const std::string &path) {
    if (path.find("..") != std::string::npos) return false;
    if (path == kInputBoost || path == kTopAppUclampMin) return true;
    for (const char *n : kVmNodes)
        if (path == std::string(kVm) + n) return true;
    for (const char *leaf : {"/read_ahead_kb", "/rq_affinity"}) {
        const std::string l = leaf;
        if (path.size() > l.size() && path.compare(path.size() - l.size(), l.size(), l) == 0 &&
            is_block_queue(path.substr(0, path.size() - l.size())))
            return true;
    }
    return false;
}

PerformancePlanner::PerformancePlanner(flux::runtime::Io io, PerfCapabilities caps,
                                       std::function<bool(const std::string &)> mitigation_allows)
    : io_(std::move(io)), caps_(std::move(caps)), mitigation_allows_(std::move(mitigation_allows)) {}

void PerformancePlanner::add_category(const std::string &category, const std::vector<Write> &writes, bool asked,
                                      PerformancePlanResult &out, std::vector<std::string> &claimed) const {
    CategoryReport rep;
    rep.category = category;
    if (!asked) {
        out.report.push_back(rep);
        return;
    }
    rep.planned = static_cast<int>(writes.size());
    if (mitigation_allows_ && !mitigation_allows_(category)) {
        rep.blocked_by_mitigation = true;
        rep.support = Support::Unsupported;
        out.report.push_back(rep);
        return;
    }
    for (const auto &w : writes) {
        if (!interface_allowed(w.path)) {
            ++rep.rejected_interface;
            continue;
        }
        if (!caps_.node_available || !caps_.node_available(w.path)) continue;
        ++rep.available;
        if (std::find(claimed.begin(), claimed.end(), w.path) != claimed.end()) continue;
        claimed.push_back(w.path);
        // NodeWriteOperation verifies by read-back and restores its snapshot: verification support.
        out.plan.operations.push_back(std::make_unique<NodeWriteOperation>(io_, category, w.path, w.value));
    }
    rep.support = rep.available > 0 ? Support::Supported : Support::Unsupported;
    out.report.push_back(rep);
}

PerformancePlanResult PerformancePlanner::plan(const PerfProfile &p, const GameContext &game) const {
    PerformancePlanResult out;
    out.plan.domain = "performance";
    out.plan.subject = game.package;
    std::vector<std::string> claimed;

    auto vm = [](const char *node, const char *v) { return Write{std::string(kVm) + node, v}; };
    std::vector<Write> memory;
    if (p.memory == "balanced") memory = {vm("vfs_cache_pressure", "100"), vm("page-cluster", "0")};
    else if (p.memory == "gaming")
        memory = {vm("swappiness", "60"), vm("vfs_cache_pressure", "80"), vm("page-cluster", "0"),
                  vm("dirty_expire_centisecs", "1500"), vm("watermark_boost_factor", "0")};
    else if (p.memory == "gaming_plus")
        memory = {vm("swappiness", "40"), vm("vfs_cache_pressure", "60"), vm("page-cluster", "0"),
                  vm("dirty_expire_centisecs", "3000"), vm("dirty_writeback_centisecs", "3000"),
                  vm("watermark_boost_factor", "0")};
    else if (p.memory != "default") out.errors.push_back("unknown memory level: " + p.memory);

    std::vector<Write> touch;
    if (p.touch == "balanced") touch = {{kInputBoost, "40"}};
    else if (p.touch == "responsive") touch = {{kInputBoost, "80"}};
    else if (p.touch == "responsive_plus") touch = {{kInputBoost, "120"}};
    else if (p.touch == "competitive") touch = {{kInputBoost, "160"}};
    else if (p.touch != "default") out.errors.push_back("unknown touch level: " + p.touch);

    std::vector<Write> storage;
    if (p.storage == "balanced" || p.storage == "gaming") {
        const std::string ra = p.storage == "gaming" ? "512" : "256";
        for (const auto &q : caps_.block_queues) {
            storage.push_back({q + "/read_ahead_kb", ra});
            if (p.storage == "gaming") storage.push_back({q + "/rq_affinity", "2"});
        }
    } else if (p.storage != "default") {
        out.errors.push_back("unknown storage level: " + p.storage);
    }

    const bool valid_memory = p.memory == "default" || !memory.empty();
    const bool valid_touch = p.touch == "default" || !touch.empty();
    const bool valid_storage = p.storage == "default" || p.storage == "balanced" || p.storage == "gaming";
    add_category("memory", memory, p.memory != "default" && valid_memory, out, claimed);
    add_category("touch", touch, p.touch != "default" && valid_touch, out, claimed);
    add_category("storage", storage, p.storage != "default" && valid_storage, out, claimed);

    // Refresh: a request for the profile script's single refresh writer, never a node write here.
    CategoryReport refresh;
    refresh.category = "refresh";
    if (p.refresh == "real" || p.refresh == "adaptive") {
        // real = leave it; adaptive stays owned by the existing adaptive-refresh path
    } else if (auto hz = refresh_hz(p.refresh, p.refresh_custom_hz)) {
        refresh.planned = 1;
        const bool offered = std::find(caps_.panel_refresh_hz.begin(), caps_.panel_refresh_hz.end(), *hz) !=
                             caps_.panel_refresh_hz.end();
        if (offered && *hz > 0) {
            refresh.available = 1;
            refresh.support = Support::Supported;
            out.refresh_target_hz = *hz;
        } else {
            refresh.support = Support::Unsupported;
        }
    } else {
        out.errors.push_back("unknown refresh selection: " + p.refresh);
    }
    out.report.push_back(refresh);
    return out;
}

RuntimePlan PerformancePlanner::plan_launch_boost(const GameContext &game) const {
    PerformancePlanResult tmp;
    tmp.plan.domain = "performance";
    tmp.plan.subject = game.package;
    std::vector<Write> writes;
    for (const auto &q : caps_.block_queues) writes.push_back({q + "/read_ahead_kb", "1024"});
    writes.push_back({kTopAppUclampMin, "50"});
    std::vector<std::string> claimed;
    add_category("launch_boost", writes, true, tmp, claimed);
    return std::move(tmp.plan);
}

// -- LaunchBoost ------------------------------------------------------------------

const char *LaunchBoost::to_string(Cancel c) {
    switch (c) {
    case Cancel::MainActive: return "main_active";
    case Cancel::Timeout: return "timeout";
    case Cancel::ProcessExit: return "process_exit";
    case Cancel::ProfileChange: return "profile_change";
    case Cancel::Watchdog: return "watchdog";
    }
    return "watchdog";
}

LaunchBoost::LaunchBoost(const PerformancePlanner &planner, int64_t max_ms)
    : planner_(planner), max_ms_(std::clamp(max_ms, kMinMs, kMaxMs)) {}

bool LaunchBoost::begin(const GameContext &game, int64_t now_ms, const std::string &tx_id,
                        flux::runtime::Transaction::JournalSink sink) {
    if (phase_ == Phase::Boosting) return false;
    tx_ = std::make_unique<flux::runtime::Transaction>(tx_id, planner_.plan_launch_boost(game), std::move(sink));
    if (!tx_->start() || tx_->state() != flux::runtime::TxState::Active) {
        // failed (already rolled back) or nothing available: nothing to undo
        phase_ = Phase::Done;
        return false;
    }
    deadline_ = now_ms + max_ms_;
    phase_ = Phase::Boosting;
    return true;
}

bool LaunchBoost::tick(int64_t now_ms) {
    if (phase_ != Phase::Boosting) return false;
    if (now_ms >= deadline_) {
        cancel(Cancel::Timeout);
        return false;
    }
    return true;
}

bool LaunchBoost::cancel(Cancel why) {
    if (phase_ != Phase::Boosting) return true;
    last_cancel_ = to_string(why);
    clean_ = tx_->finish();
    phase_ = Phase::Done;
    return clean_;
}

} // namespace flux::perf
