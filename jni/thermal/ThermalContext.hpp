// Neutral read-only thermal context (Step 8.9).
//
// Zairenkai observes thermal state; it never controls it. Synrei (HiCo) owns thermal policy,
// thermal state, the safety ceiling and thermal control. This header knows nothing about Synrei:
// adapters (SynreiThermalAdapter) fill ThermalSnapshot, and ThermalHistory turns verified
// snapshots into BottleneckModel thermal evidence.
//
// Rules: missing data is UNKNOWN; an unreadable source is UNKNOWN + unreadable; stale or
// unverifiable context is never current evidence; a constraint comes only from the thermal
// authority's own state, never from a high temperature.
#pragma once

#include "BottleneckModel.hpp"
#include "CapabilityContext.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace flux::thermal {

/// Whether the thermal authority reports that it is constraining performance.
enum class Constraint { Unknown, Unconstrained, Constrained };
const char *to_string(Constraint c);

struct TempReading {
    std::optional<double> celsius;
    bool readable = false;
    std::string note; // why UNKNOWN
};

struct ThermalSnapshot {
    int64_t timestamp_ms = 0;     // steady clock when read (session clock)
    int64_t source_time_s = 0;    // authority's own timestamp (epoch seconds), 0 = unknown
    std::string state;            // authority state, e.g. "boost", "safety"; "" = unknown
    std::string reason;           // authority's reason text
    Constraint constraint = Constraint::Unknown;
    std::optional<double> headroom_c; // nullopt = not published
    TempReading cpu, gpu, battery;
    std::optional<double> slope_c_per_min; // CPU temperature slope between verified snapshots
    std::string source;           // e.g. "synrei:/dev/hico/state"
    flux::context::Confidence confidence = flux::context::Confidence::None;
    bool readable = false;        // the source was read and parsed
    bool verified = false;        // fresh and the authority process is alive
    std::string note;             // why unavailable / stale / unverified
    bool usable() const { return readable && verified && constraint != Constraint::Unknown; }
};

/// Bounded per-session record of snapshots; the BottleneckModel's ThermalContext.
/// at(ts) returns evidence only from a usable snapshot taken at or before ts and no older than max_gap_ms.
class ThermalHistory final : public flux::bottleneck::ThermalContext {
  public:
    explicit ThermalHistory(int64_t max_gap_ms = 5000, size_t capacity = 900)
        : max_gap_ms_(max_gap_ms), capacity_(capacity) {}
    void add(const ThermalSnapshot &s);
    void clear() { items_.clear(); }
    const std::deque<ThermalSnapshot> &items() const { return items_; }
    std::optional<flux::bottleneck::ThermalState> at(int64_t timestamp_ms) const override;

  private:
    int64_t max_gap_ms_;
    size_t capacity_;
    std::deque<ThermalSnapshot> items_;
};

} // namespace flux::thermal
