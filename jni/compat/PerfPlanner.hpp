#pragma once

// Turns the per-game memory / touch / storage / launch-boost selections into
// reversible node writes.
//
// "default" means "leave whatever Flux already does alone": the existing
// flux_profiler.sh tiers keep owning the baseline, this layer only adds an
// explicit per-game choice on top. Every write is gated on the node existing, and
// ZRAM sizing/algorithm is deliberately not touched here (zram_tune owns it).

#include <memory>
#include <string>
#include <vector>

#include "Runtime.hpp"

namespace flux::compat {

struct PerfPlanInput {
    std::string memory = "default", touch = "default", storage = "default";
    /// Internal block-device queue directories, e.g. "/sys/block/sda/queue". Enumerated
    /// by the caller (removable media excluded) since Io has no directory listing.
    std::vector<std::string> block_queues;
    /// Category names the device mitigation store forbids on this device.
    std::function<bool(const std::string &category)> mitigation_allows;
};

struct PlannedNode {
    std::string category, path, value;
};

/// Pure planning: which writes each selection asks for, before support is checked.
std::vector<PlannedNode> plan_memory(const std::string &level);
std::vector<PlannedNode> plan_touch(const std::string &level);
std::vector<PlannedNode> plan_storage(const std::string &level, const std::vector<std::string> &queues);
std::vector<PlannedNode> plan_launch_boost(const std::vector<std::string> &queues);

/// Support status of a category, for the UI: AUTO (nothing asked), SUPPORTED, UNSUPPORTED.
enum class Support { Auto, Supported, Unsupported };
const char *to_string(Support s);

struct CategoryStatus {
    std::string category;
    Support support = Support::Auto;
    int planned = 0, available = 0;
    bool blocked_by_mitigation = false;
};

/// Build actions for the selected levels. Nodes missing on this kernel are dropped,
/// duplicate paths keep the first request, and categories the device mitigation
/// forbids are skipped and reported rather than silently bypassed.
std::vector<std::unique_ptr<Action>> build_actions(const Io &io, const PerfPlanInput &in,
                                                   std::vector<CategoryStatus> &status);

/// Short-lived boost around process creation. Bounded by a deadline and cancelable;
/// the writes it makes are ordinary Actions, so cancel() restores them.
class LaunchBoost {
public:
    enum class Phase { Idle, Boosting, Done };
    enum class Cancel { MainActive, Timeout, ProcessExit, ProfileChange, Watchdog };

    LaunchBoost(Io io, std::vector<std::string> block_queues, int64_t max_ms = 8000)
        : io_(std::move(io)), queues_(std::move(block_queues)), max_ms_(max_ms) {}

    bool begin(int64_t now_ms);
    /// True while boosting and the deadline has not passed. Call `tick` from the
    /// daemon's existing loop; it never sleeps or polls on its own.
    bool tick(int64_t now_ms);
    bool cancel(Cancel why);

    Phase phase() const { return phase_; }
    bool restored_clean() const { return clean_; }
    static const char *to_string(Cancel c);
    const std::string &last_cancel() const { return last_cancel_; }

private:
    Io io_;
    std::vector<std::string> queues_;
    int64_t max_ms_;
    int64_t deadline_ = 0;
    Phase phase_ = Phase::Idle;
    std::unique_ptr<Transaction> tx_;
    bool clean_ = true;
    std::string last_cancel_;
};

} // namespace flux::compat
