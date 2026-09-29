#include "PerfPlanner.hpp"

#include <set>

namespace flux::compat {

namespace {
constexpr const char *kVm = "/proc/sys/vm/";
}

const char *to_string(Support s) {
    switch (s) {
    case Support::Auto: return "auto";
    case Support::Supported: return "supported";
    case Support::Unsupported: return "unsupported";
    }
    return "auto";
}

std::vector<PlannedNode> plan_memory(const std::string &level) {
    auto vm = [](const char *node, const char *v) { return PlannedNode{"memory", std::string(kVm) + node, v}; };
    if (level == "balanced")
        return {vm("vfs_cache_pressure", "100"), vm("page-cluster", "0")};
    if (level == "gaming")
        return {vm("swappiness", "60"), vm("vfs_cache_pressure", "80"), vm("page-cluster", "0"),
                vm("dirty_expire_centisecs", "1500"), vm("watermark_boost_factor", "0")};
    if (level == "gaming_plus")
        return {vm("swappiness", "40"), vm("vfs_cache_pressure", "60"), vm("page-cluster", "0"),
                vm("dirty_expire_centisecs", "3000"), vm("dirty_writeback_centisecs", "3000"),
                vm("watermark_boost_factor", "0")};
    return {};
}

std::vector<PlannedNode> plan_touch(const std::string &level) {
    // cpu_boost input boost is the one input-latency knob with a stable path across
    // msm kernels; vendor touch panel modes stay with flux_touch in flux_profiler.sh.
    const char *node = "/sys/module/cpu_boost/parameters/input_boost_ms";
    if (level == "balanced") return {{"touch", node, "40"}};
    if (level == "responsive") return {{"touch", node, "80"}};
    if (level == "responsive_plus") return {{"touch", node, "120"}};
    if (level == "competitive") return {{"touch", node, "160"}};
    return {};
}

std::vector<PlannedNode> plan_storage(const std::string &level, const std::vector<std::string> &queues) {
    std::vector<PlannedNode> out;
    const char *ra = level == "gaming" ? "512" : level == "balanced" ? "256" : nullptr;
    if (!ra) return out;
    for (const auto &q : queues) {
        out.push_back({"storage", q + "/read_ahead_kb", ra});
        if (level == "gaming") out.push_back({"storage", q + "/rq_affinity", "2"});
    }
    return out;
}

std::vector<PlannedNode> plan_launch_boost(const std::vector<std::string> &queues) {
    std::vector<PlannedNode> out;
    for (const auto &q : queues) out.push_back({"launch_boost", q + "/read_ahead_kb", "1024"});
    out.push_back({"launch_boost", "/dev/cpuctl/top-app/cpu.uclamp.min", "50"});
    return out;
}

std::vector<std::unique_ptr<Action>> build_actions(const Io &io, const PerfPlanInput &in,
                                                   std::vector<CategoryStatus> &status) {
    std::vector<std::unique_ptr<Action>> out;
    std::set<std::string> claimed;
    auto add = [&](const std::string &cat, const std::vector<PlannedNode> &nodes, bool asked) {
        CategoryStatus st;
        st.category = cat;
        if (!asked) { status.push_back(st); return; }
        st.planned = static_cast<int>(nodes.size());
        if (in.mitigation_allows && !in.mitigation_allows(cat)) {
            st.blocked_by_mitigation = true;
            st.support = Support::Unsupported;
            status.push_back(st);
            return;
        }
        for (const auto &n : nodes) {
            if (!io.exists || !io.exists(n.path)) continue;
            ++st.available;
            if (!claimed.insert(n.path).second) continue;
            out.push_back(std::make_unique<NodeWrite>(io, n.category, n.path, n.value));
        }
        st.support = st.available > 0 ? Support::Supported : Support::Unsupported;
        status.push_back(st);
    };
    add("memory", plan_memory(in.memory), in.memory != "default");
    add("touch", plan_touch(in.touch), in.touch != "default");
    add("storage", plan_storage(in.storage, in.block_queues), in.storage != "default");
    return out;
}

// -- LaunchBoost ---------------------------------------------------------------

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

bool LaunchBoost::begin(int64_t now_ms) {
    if (phase_ == Phase::Boosting) return false; // one boost at a time
    tx_ = std::make_unique<Transaction>("launch_boost");
    std::set<std::string> claimed;
    for (const auto &n : plan_launch_boost(queues_))
        if (claimed.insert(n.path).second) tx_->add(std::make_unique<NodeWrite>(io_, n.category, n.path, n.value));
    if (!tx_->start()) { // rolled back by the transaction
        phase_ = Phase::Done;
        return false;
    }
    if (tx_->state() != ContextState::Active) { // no node available: nothing to boost, nothing to undo
        phase_ = Phase::Done;
        return false;
    }
    deadline_ = now_ms + max_ms_;
    phase_ = Phase::Boosting;
    return true;
}

bool LaunchBoost::tick(int64_t now_ms) {
    if (phase_ != Phase::Boosting) return false;
    if (now_ms >= deadline_) { cancel(Cancel::Timeout); return false; }
    return true;
}

bool LaunchBoost::cancel(Cancel why) {
    if (phase_ != Phase::Boosting) return true;
    last_cancel_ = to_string(why);
    clean_ = tx_->finish();
    phase_ = Phase::Done;
    return clean_;
}

} // namespace flux::compat
