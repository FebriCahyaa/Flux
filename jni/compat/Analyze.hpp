#pragma once

// Read-only analysis entry point behind `fluxd compat_analyze <package>`.
// Takes the documents as text so it is testable without files; the CLI only
// reads them from disk. It never writes to the device.

#include <optional>
#include <string>

#include "CompatTypes.hpp"
#include "GameProfile.hpp"
#include "Hardware.hpp"
#include "Resolver.hpp"
#include "Arming.hpp"

namespace flux::compat {

struct AnalyzeInputs {
    std::string capabilities_json; ///< canonical capability model; empty = hardware unknown
    std::string library_json;      ///< presets + identities; empty = built-in defaults only
    std::string known_games_json;  ///< curated requirements; empty = none
    std::string profiles_json;     ///< {"<package>": <profile>, ...}; empty = none
    std::string provider_state = "unavailable"; ///< unavailable | not_configured | unsupported | installed | loaded; reported, never acted on here
};

/// Everything the resolver needs for one package, parsed from the documents above.
struct ResolvedInputs {
    EffectiveProfile profile;
    std::optional<GameRequirement> known;
    RealHardware hw;
    ProfileLibrary lib;
    bool has_profile = false;
    std::string warning; ///< non-fatal problem (e.g. unknown preset); resolution still ran
};

/**
 * Parse the documents and resolve the effective profile. @p unprofiled_mode is the mode
 * used when the package has no entry in profiles_json and no override was given: the
 * analysis tool passes Auto (look, don't touch), the daemon passes Real (an unprofiled
 * game must behave exactly as it did before the Game Runtime existed).
 */
bool build_inputs(const std::string &package, std::optional<Mode> mode_override, Mode unprofiled_mode,
                  const AnalyzeInputs &in, ResolvedInputs &out, std::string &error);

/// Package names that have an entry in game_profiles.json (invalid names are skipped).
std::vector<std::string> profiled_packages(const std::string &profiles_json);

struct ArmReport {
    std::vector<std::string> armed, disarmed;
    std::vector<std::pair<std::string, std::string>> failed; ///< package, reason
};

/// Resolve every profiled package and arm (or disarm) its plan. This is the one place that decides
/// which packages the provider may touch: the resolver's output, nothing else. With tweaks
/// disabled everything is disarmed, matching the rest of the Game Runtime.
ArmReport arm_all(Arming &arming, const AnalyzeInputs &in, bool tweaks_disabled);

/// Always returns a JSON object: {"ok":bool,"error":"","effective":{..},"resolution":{..}}.
std::string analyze_package(const std::string &package, std::optional<Mode> mode_override,
                            const AnalyzeInputs &in);

} // namespace flux::compat
