// Inputs of the Decision Engine (Step 8.13). Everything is read-only evidence produced by the
// existing layers; nothing here measures, probes or writes. Missing evidence is represented as
// absent (std::nullopt / empty) and stays UNKNOWN in decisions.
#pragma once

#include "BottleneckResult.hpp"
#include "CapabilityContext.hpp"
#include "ObservatoryAnalyzer.hpp"
#include "ThermalContext.hpp"
#include "Transaction.hpp"

#include <optional>
#include <string>
#include <vector>

namespace flux::policy {

/// Mirrors FluxProfileMode (jni/include/Flux.hpp). Intent only: never authority to write.
enum class ProfileMode { Unknown, PerfCommon, Performance, PerformanceLite, Balance, Powersave };
const char *to_string(ProfileMode m);

struct ProfileIntent {
    ProfileMode mode = ProfileMode::Unknown;
    bool requests_performance() const { return mode == ProfileMode::Performance; }
};

/// FPS facts from the existing FPS observation bridge / bottleneck evidence. Hz and FPS stay apart.
struct FpsEvidence {
    std::optional<double> target_refresh_hz;     // display target (Hz)
    std::optional<double> refresh_capability_hz; // panel capability (Hz)
    std::optional<double> observed_fps;          // frames presented (FPS)
    std::optional<int> shortfall_samples, fps_samples; // samples below target / samples with FPS
    std::string source;
    bool shortfall_observed() const { return shortfall_samples && fps_samples && *fps_samples > 0 && *shortfall_samples > 0; }
};

/// Runtime / transaction state as reported by the existing GameRuntime and Transaction Engine.
struct RuntimeEvidence {
    bool game_active = false;
    flux::runtime::TxState transaction = flux::runtime::TxState::Inactive;
    std::string transaction_id;
    bool recovery_failed = false; // RecoveryReport not clean: journal kept
    /// "<ACTION>:<target>" of the policy transaction that is active ("" = unknown / none). Lets the
    /// engine tell an applied MITIGATE (aligned with thermal safety) from an applied BOOST.
    std::string active_intervention;
    /// Thermal hold (B-42): entered when a BOOST was restored under Synrei safety; cleared by the
    /// live loop only on a fresh, verified Synrei state other than safety. While set: no BOOST.
    bool thermal_hold = false;
};

struct HistoricalEvidence {
    std::vector<flux::observatory::Pattern> patterns; // from ObservatoryAnalyzer::history
};

struct PolicyInputs {
    std::string session_id;
    int64_t evaluation_time_ms = 0; // supplied by the caller; the engine reads no clock
    const flux::context::CapabilityContext *capabilities = nullptr;
    std::optional<flux::bottleneck::BottleneckResult> bottleneck;
    std::optional<flux::thermal::ThermalSnapshot> thermal;
    FpsEvidence fps;
    ProfileIntent profile;
    RuntimeEvidence runtime;
    HistoricalEvidence history;
};

/// One piece of evidence a decision cites. `source` names the producing layer; `ref` is the
/// exact identifier there (capability id, bottleneck evidence metric, event id, ...); `value`
/// is copied verbatim.
struct EvidenceRef {
    std::string source; // capability | bottleneck | thermal | fps | runtime | profile | history
    std::string ref;
    std::string value;
};

} // namespace flux::policy
