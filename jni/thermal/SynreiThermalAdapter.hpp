// Synrei (HiCo) thermal context adapter (Step 8.9) — read-only.
//
// Reads what hicod already publishes for other processes: /dev/hico/state (key=value lines:
// state, reason, updated (epoch s), pid, cpu_temp, gpu_temp, battery_temp, level, version). The
// file is removed when hicod stops. Through flux::kernel::ReadOnlyFs only: no write exists.
//
// Constraint mapping (Synrei's own state, never temperatures):
//   safety          -> Constrained   (Synrei's safety guard restored thermal protection)
//   boost           -> Unconstrained (Synrei reports throttling disabled for the game)
//   relaxed, idle, suspended, disabled, other -> Unknown (vendor/stock thermal decides; Synrei
//                      does not publish whether it throttles)
#pragma once

#include "KernelIntelligence.hpp"
#include "ThermalContext.hpp"

#include <functional>

namespace flux::thermal {

inline constexpr const char *kSynreiStatePath = "dev/hico/state";

class SynreiThermalAdapter {
  public:
    /// wall_clock_s: epoch seconds (to check Synrei's `updated`); max_age_s: older = stale.
    SynreiThermalAdapter(const flux::kernel::ReadOnlyFs &fs, std::function<int64_t()> wall_clock_s,
                         int64_t max_age_s = 15);
    /// Never throws for missing/unreadable/malformed input; returns an UNKNOWN snapshot instead.
    ThermalSnapshot read(int64_t now_ms);

  private:
    const flux::kernel::ReadOnlyFs &fs_;
    std::function<int64_t()> wall_clock_s_;
    int64_t max_age_s_;
    // Slope: previous verified CPU reading from the same Synrei process.
    int previous_pid_ = 0;
    int64_t previous_ms_ = 0;
    std::optional<double> previous_cpu_;
};

} // namespace flux::thermal
