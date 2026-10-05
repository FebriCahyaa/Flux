#include "ThermalContext.hpp"

namespace flux::thermal {

const char *to_string(Constraint c) {
    switch (c) {
    case Constraint::Constrained: return "constrained";
    case Constraint::Unconstrained: return "unconstrained";
    case Constraint::Unknown: break;
    }
    return "unknown";
}

void ThermalHistory::add(const ThermalSnapshot &s) {
    items_.push_back(s);
    while (items_.size() > capacity_) items_.pop_front();
}

std::optional<flux::bottleneck::ThermalState> ThermalHistory::at(int64_t timestamp_ms) const {
    // The latest snapshot at or before the sample decides; an older one is superseded.
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        if (it->timestamp_ms > timestamp_ms) continue;
        if (timestamp_ms - it->timestamp_ms > max_gap_ms_ || !it->usable()) return std::nullopt;
        flux::bottleneck::ThermalState s;
        s.timestamp_ms = it->timestamp_ms;
        s.throttling = it->constraint == Constraint::Constrained;
        s.source = "synrei";
        return s; // cap_ratio stays unknown: Synrei does not publish one
    }
    return std::nullopt;
}

} // namespace flux::thermal
