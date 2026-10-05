// Zairenkai brand layer (Phase 4.5) — human-readable labels and frozen compatibility identifiers.
//
// Public identity:  Zairenkai (platform), Synrei Thermal Intelligence, Zairenkai Intelligence.
// Internal codename: Aeyrin (never a public product name).
// Legacy / compatibility identities: Flux (runtime), HiCo (thermal backend), SynthesisCore
// (package / component). A legacy identifier does not mean the implementation is obsolete.
//
// Labels may change; the compatibility identifiers below are FROZEN. They are machine-readable
// (paths, package and module IDs, binary names, log tags) and change only through an explicit
// migration mechanism, never for branding.
#pragma once

#include <string>
#include <string_view>

namespace zairenkai::brand {

// -- Public labels (human-readable only) ------------------------------------------------------
inline constexpr std::string_view kPlatform = "Zairenkai";
inline constexpr std::string_view kThermal = "Synrei Thermal Intelligence";
inline constexpr std::string_view kIntelligence = "Zairenkai Intelligence";
inline constexpr std::string_view kCodename = "Aeyrin"; // internal only
inline constexpr std::string_view kLegacyRuntimeLabel = "Flux Tweaks";

// -- Compatibility identifiers (FROZEN) -------------------------------------------------------
inline constexpr std::string_view kModuleId = "flux";
inline constexpr std::string_view kDaemonBinary = "fluxd";
inline constexpr std::string_view kPublicBinary = "zairenkai"; // alias (symlink) of fluxd
inline constexpr std::string_view kLegacyConfigDir = "/data/adb/.config/flux";
inline constexpr std::string_view kConfigDir = "/data/adb/.config/zairenkai"; // telemetry, journals
inline constexpr std::string_view kHicoConfigDir = "/data/adb/.config/hico";
inline constexpr std::string_view kHicoStatePath = "/dev/hico/state";
inline constexpr std::string_view kIntelligencePackage = "com.febricahyaa.synthesiscore";
inline constexpr std::string_view kLogTag = "FluxTweaks";

/// Name to show in CLI help for the binary the user invoked: "zairenkai" when called through the
/// public alias, otherwise "fluxd". Only presentation; both run the same handlers.
std::string command_name(std::string_view argv0);

/// "Zairenkai CLI" banner line, mentioning the legacy runtime name.
std::string cli_banner();

/// Version line: "Zairenkai <version> (Flux Tweaks runtime)".
std::string version_line(std::string_view version);

} // namespace zairenkai::brand
