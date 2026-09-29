#pragma once

// Per-game profile schema and inheritance.
//
//   Global default -> preset(s) named by "extends" -> game profile -> runtime override
//
// Every field is optional in a layer; a later layer replaces only what it sets.
// A game therefore overrides one subsystem without copying the rest.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "CompatTypes.hpp"

namespace flux::compat {

struct PerfSettings {
    std::optional<std::string> profile;  ///< performance | balance | powersave
    std::optional<std::string> memory;   ///< default | balanced | gaming | gaming_plus
    std::optional<std::string> touch;    ///< default | balanced | responsive | responsive_plus | competitive | custom
    std::optional<std::string> storage;  ///< default | balanced | gaming
    std::optional<std::string> refresh;  ///< real | adaptive | hz60 | hz90 | hz120 | custom
    std::optional<bool> launch_boost;
};

struct CompatSettings {
    std::optional<std::string> mode;     ///< real | auto | compatibility | advanced | custom
    std::optional<std::string> device_profile, cpu_profile, gpu_profile, display_profile; ///< identity library names; "real_*" = real
    /// Which processes of the package receive the identity: main (default, exactly the package name),
    /// listed (exact names in `processes`) or all (the package's own :sub processes too).
    std::optional<std::string> process_scope;
    std::optional<std::vector<std::string>> processes;
};

/// One layer of the inheritance chain.
struct GameProfile {
    std::string package;  ///< empty for presets
    std::string extends;  ///< preset name, empty for none
    PerfSettings perf;
    CompatSettings compat;
};

/// Fully resolved: every field concrete.
struct EffectiveProfile {
    std::string package;
    std::string performance = "balance";
    std::string memory = "default", touch = "default", storage = "default", refresh = "real";
    bool launch_boost = false;
    Mode mode = Mode::Real;
    std::string device_profile = "real_device", cpu_profile = "real_cpu", gpu_profile = "real_gpu",
                display_profile = "real_display";
    std::string process_scope = "main";
    std::vector<std::string> processes;
    std::vector<std::string> chain; ///< layers that contributed, for diagnostics
};

/// A named identity (device / cpu / gpu) a game may adopt inside its own process.
struct IdentityProfile {
    std::string name;
    Layer layer = Layer::Device;
    std::map<std::string, std::string> fields;
};

class ProfileLibrary {
public:
    std::map<std::string, GameProfile> presets;
    std::map<std::string, IdentityProfile> identities;

    /// Parse the shipped library document ({"presets":{..},"identities":{..}}).
    bool load_json(const std::string &text, std::string &error);
    std::string to_json() const;

    /**
     * @brief Resolve the effective profile for @p package.
     * @param global    lowest layer (the user's global defaults), may be empty
     * @param game      the game's own entry, if any
     * @param runtime   transient override (e.g. the user picked a mode for this session)
     * `extends` chains are followed with cycle detection; an unknown preset or a
     * cycle is reported through @p error and resolution stops at that layer.
     */
    EffectiveProfile resolve(const std::string &package, const GameProfile &global,
                             const std::optional<GameProfile> &game, const GameProfile &runtime,
                             std::string &error) const;
};

/// Parse one profile object. Rejects unknown enum values instead of guessing.
bool parse_profile(const std::string &json, GameProfile &out, std::string &error);
std::string profile_to_json(const GameProfile &p);

/// Identity profiles may not claim capabilities: a CPU identity that lists ISA
/// features, or a GPU identity that lists Vulkan extensions/features, is refused.
bool validate_identity(const IdentityProfile &id, std::string &error);

} // namespace flux::compat
