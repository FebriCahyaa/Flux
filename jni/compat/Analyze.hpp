#pragma once

// Read-only analysis entry point behind `fluxd compat_analyze <package>`.
// Takes the documents as text so it is testable without files; the CLI only
// reads them from disk. It never writes to the device.

#include <optional>
#include <string>

#include "CompatTypes.hpp"

namespace flux::compat {

struct AnalyzeInputs {
    std::string capabilities_json; ///< canonical capability model; empty = hardware unknown
    std::string library_json;      ///< presets + identities; empty = built-in defaults only
    std::string known_games_json;  ///< curated requirements; empty = none
    std::string profiles_json;     ///< {"<package>": <profile>, ...}; empty = none
    BackendState zygisk_state = BackendState::Unavailable; ///< reported, never acted on here
};

/// Always returns a JSON object: {"ok":bool,"error":"","effective":{..},"resolution":{..}}.
std::string analyze_package(const std::string &package, std::optional<Mode> mode_override,
                            const AnalyzeInputs &in);

} // namespace flux::compat
