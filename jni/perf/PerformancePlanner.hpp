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

// Performance Planner (docs/architecture/PERFORMANCE_PLANNER.md).
//
// Decides WHAT to change for a game: turns a per-game performance profile plus the device's
// probed capabilities into a RuntimePlan(domain="performance"). It never writes a node itself;
// the Runtime Transaction Engine applies, verifies and restores the plan.

#include "CapabilityContext.hpp"
#include "Transaction.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flux::perf {

/// Read-only facts about the device, gathered by a probe before planning.
struct PerfCapabilities {
    /// True when the node exists and is readable (capability check). Never writes.
    std::function<bool(const std::string &path)> node_available;
    /// Internal block-device queue dirs ("/sys/block/sda/queue"); removable media excluded by the probe.
    std::vector<std::string> block_queues;
    /// Refresh rates the panel reports (Hz). Empty = unknown.
    std::vector<int> panel_refresh_hz;
    /// Shared capability context (Step 7.5). Carried for consumers; the planner's decisions do
    /// not read it yet (no policy change). Null = not provided.
    std::shared_ptr<const flux::context::CapabilityContext> context;
};

/// Per-game selections. "default" / "real" means: leave the existing Flux profile alone.
struct PerfProfile {
    std::string memory = "default";  ///< default | balanced | gaming | gaming_plus
    std::string touch = "default";   ///< default | balanced | responsive | responsive_plus | competitive
    std::string storage = "default"; ///< default | balanced | gaming
    std::string refresh = "real";    ///< real | adaptive | hz60 | hz90 | hz120 | hz144 | custom
    int refresh_custom_hz = 0;       ///< used when refresh == "custom"
    bool launch_boost = false;
};

struct GameContext {
    std::string package;
};

enum class Support { Auto, Supported, Unsupported };
const char *to_string(Support s);

struct CategoryReport {
    std::string category;
    Support support = Support::Auto;
    int planned = 0;           ///< writes the selection asks for
    int available = 0;         ///< of those, nodes present on this device
    int rejected_interface = 0; ///< paths outside the allowed interfaces (never planned)
    bool blocked_by_mitigation = false;
};

struct PerformancePlanResult {
    flux::runtime::RuntimePlan plan;  ///< domain "performance"
    int refresh_target_hz = 0;        ///< 0 = no request; only a rate the panel reports
    std::vector<CategoryReport> report;
    std::vector<std::string> errors;  ///< invalid selections (rejected, never guessed)
};

class PerformancePlanner {
public:
    /// @param io  handed to the operations the planner creates; the planner itself never writes.
    /// @param mitigation_allows  device-mitigation gate per category; null allows everything.
    PerformancePlanner(flux::runtime::Io io, PerfCapabilities caps,
                       std::function<bool(const std::string &category)> mitigation_allows = nullptr);

    PerformancePlanResult plan(const PerfProfile &profile, const GameContext &game) const;

    /// Plan for the short boost around process creation.
    flux::runtime::RuntimePlan plan_launch_boost(const GameContext &game) const;

    /// Interface check: only these kernel interfaces may ever be planned.
    static bool interface_allowed(const std::string &path);

    /// The capability context this planner was given (null when none).
    const flux::context::CapabilityContext *capability_context() const { return caps_.context.get(); }

private:
    struct Write {
        std::string path, value;
    };
    void add_category(const std::string &category, const std::vector<Write> &writes, bool asked,
                      PerformancePlanResult &out, std::vector<std::string> &claimed) const;

    flux::runtime::Io io_;
    PerfCapabilities caps_;
    std::function<bool(const std::string &)> mitigation_allows_;
};

/// Short-lived boost around a game launch: bounded, cancelable, snapshot-protected, rolled back
/// through the Transaction Engine. Driven by the caller's loop through tick(); never sleeps.
class LaunchBoost {
public:
    enum class Phase { Idle, Boosting, Done };
    enum class Cancel { MainActive, Timeout, ProcessExit, ProfileChange, Watchdog };

    static constexpr int64_t kMinMs = 1000;
    static constexpr int64_t kMaxMs = 15000;

    /// @param max_ms clamped to [kMinMs, kMaxMs].
    LaunchBoost(const PerformancePlanner &planner, int64_t max_ms = 8000);

    /// Start boosting. False when already boosting, when nothing is available, or when the
    /// transaction failed (already rolled back).
    bool begin(const GameContext &game, int64_t now_ms, const std::string &tx_id,
               flux::runtime::Transaction::JournalSink sink = nullptr,
               flux::runtime::TxObserver observer = nullptr);
    /// True while boosting; ends the boost (restore) once the deadline has passed.
    bool tick(int64_t now_ms);
    /// End the boost and restore. True when everything was restored cleanly (or nothing was active).
    bool cancel(Cancel why);

    Phase phase() const { return phase_; }
    int64_t deadline_ms() const { return deadline_; }
    int64_t max_ms() const { return max_ms_; }
    bool restored_clean() const { return clean_; }
    const std::string &last_cancel() const { return last_cancel_; }
    const flux::runtime::Transaction *transaction() const { return tx_.get(); }
    static const char *to_string(Cancel c);

private:
    const PerformancePlanner &planner_;
    int64_t max_ms_;
    int64_t deadline_ = 0;
    Phase phase_ = Phase::Idle;
    std::unique_ptr<flux::runtime::Transaction> tx_;
    bool clean_ = true;
    std::string last_cancel_;
};

} // namespace flux::perf
