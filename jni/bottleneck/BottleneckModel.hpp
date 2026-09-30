// Zairenkai runtime bottleneck observation (Step 8.6) — read-only, evidence-based.
//
// Turns what was measured during a session into *observations* about possible constraints:
// CPU, GPU, thermal, memory, storage, display — or Unknown. Every observation carries its evidence,
// confidence, source and timestamp. Nothing here acts: no optimisation, no policy, no profile,
// GPU or thermal change. Consumers (a future policy engine, the Observatory, the WebUI) only read.
//
// States: CONFIRMED > LIKELY > POSSIBLE > UNKNOWN. CONFIRMED needs sustained evidence, a measured
// frame deficit and a measured exclusion of the competing resource; missing data never raises a state.
#pragma once

#include "CapabilityContext.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flux::bottleneck {

enum class Kind { Cpu, Gpu, Thermal, Memory, Storage, Display, Unknown };
enum class State { Unknown, Possible, Likely, Confirmed };
const char *to_string(Kind k);
const char *to_string(State s);

/// One measurement tick of a running session. Every metric is optional: absent = not measured.
struct RuntimeSample {
    int64_t timestamp_ms = 0;
    std::optional<double> fps;             // frames actually presented (SessionRecorder), not refresh
    std::optional<double> target_hz;       // what the game should reach (panel refresh / game cap)
    std::optional<double> cpu_busiest_core; // 0..1 utilisation of the busiest core
    std::optional<double> cpu_freq_ratio;  // 0..1 current / max frequency of the busiest cluster
    std::optional<double> gpu_busy;        // 0..1
    std::optional<double> gpu_freq_ratio;  // 0..1 current / max GPU frequency
    std::optional<double> mem_psi_some;    // /proc/pressure/memory some avg10 (%)
    std::optional<double> mem_available_mb;
    std::optional<double> io_psi_some;     // /proc/pressure/io some avg10 (%)
};

struct SessionData {
    std::string package;
    std::vector<RuntimeSample> samples;
};

/// Performance state at the time of observation (recorded as evidence, never changed).
struct PerformanceState {
    std::string profile;       // e.g. "performance", "balance", "powersave"
    bool lite_mode = false;
    int refresh_request_hz = 0; // what the runtime asked the panel for, 0 = none
};

/// Interface for the future Synrei thermal authority. Flux only reads it.
struct ThermalState {
    int64_t timestamp_ms = 0;
    bool throttling = false;
    std::optional<double> cap_ratio; // 0..1 thermal frequency cap / max (1 = uncapped)
    std::string source;              // e.g. "synrei"
};
class ThermalContext {
  public:
    virtual ~ThermalContext() = default;
    /// Thermal state at (or nearest before) `timestamp_ms`; nullopt = not known.
    virtual std::optional<ThermalState> at(int64_t timestamp_ms) const = 0;
};

struct BottleneckInputs {
    const flux::context::CapabilityContext *context = nullptr;
    SessionData session;
    PerformanceState performance;
    const ThermalContext *thermal = nullptr;
    int64_t now_ms = 0;
};

struct Evidence {
    std::string metric;    // e.g. "cpu_busiest_core"
    std::string value;     // e.g. "0.97 in 5/5 samples"
    std::string threshold; // e.g. ">= 0.90"
    std::string source;    // "session", "synrei", "capability_context", "performance_state"
    int64_t timestamp_ms = 0;
};

struct Observation {
    Kind kind = Kind::Unknown;
    State state = State::Unknown;
    flux::context::Confidence confidence = flux::context::Confidence::None;
    std::string source;
    int64_t timestamp_ms = 0;
    std::vector<Evidence> evidence;
    std::string note;
};

struct Assessment {
    int64_t timestamp_ms = 0;
    size_t samples = 0;
    std::vector<Observation> observations; // one per Kind except Unknown, fixed order
    Kind primary = Kind::Unknown;
    State primary_state = State::Unknown;
    bool conflict = false;
    std::string note;
    const Observation *get(Kind k) const;
};

// Thresholds (documented in BOTTLENECK_MODEL.md).
inline constexpr size_t kMinSamples = 3;     // below: at most POSSIBLE
inline constexpr size_t kConfirmSamples = 5; // CONFIRMED needs at least this many
inline constexpr double kSustained = 0.8;    // share of samples for CONFIRMED
inline constexpr double kMajority = 0.6;     // share of samples for LIKELY
inline constexpr double kDeficit = 0.9;      // fps < target * this = frame deficit
inline constexpr double kSaturated = 0.9;    // busy / utilisation
inline constexpr double kAtMaxFreq = 0.95;   // freq ratio
inline constexpr double kIdle = 0.7;         // below = measured not saturated (exclusion)
inline constexpr double kThermalCap = 0.9;
inline constexpr double kMemPsi = 10.0, kIoPsi = 20.0, kMemLowMb = 300.0;
inline constexpr double kAtRefresh = 0.97;   // fps >= refresh * this = frames pinned to the panel

Assessment assess(const BottleneckInputs &in);

/// Observations as context facts (domain "bottleneck", publisher "bottleneck"); read-only
/// information for consumers, never a capability claim.
std::vector<flux::context::CapabilityFact> to_facts(const Assessment &a);

} // namespace flux::bottleneck
