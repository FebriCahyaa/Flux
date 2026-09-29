#pragma once

// Universal compatibility resolver.
//
//   requirement (what the game checks)  -  real hardware  =  gap
//   gap  ->  minimum set of layers to override  ->  plan
//
// Known games arrive with a curated requirement; unknown games arrive with an
// empty one and are answered with an honest "no reliable profile" analysis plus
// the real capabilities, never with a guessed spoof.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "CompatTypes.hpp"
#include "GameProfile.hpp"
#include "Hardware.hpp"

namespace flux::compat {

struct Gate {
    GateKind kind = GateKind::Unknown;
    std::string detail;
    std::string identity; ///< curated identity profile that satisfies this gate, if the database knows one
};

struct GameRequirement {
    std::string package;
    bool known = false;              ///< true when a curated entry exists
    Confidence confidence = Confidence::Unknown;
    std::vector<Gate> gates;
    double target_fps = 0;           ///< what the unlock is meant to reach (0 = none)
    bool needs_vulkan = false;
};

/// Curated database: deterministic answer for games we have looked at.
class KnownGameDb {
public:
    bool load_json(const std::string &text, std::string &error);
    std::optional<GameRequirement> find(const std::string &package) const;
    size_t size() const { return games_.size(); }

private:
    std::map<std::string, GameRequirement> games_;
};

struct Blocker {
    GateKind kind;
    std::string reason;
};

/// Why a layer is (or is not) part of the plan; surfaced verbatim in the WebUI.
struct LayerDecision {
    Layer layer;
    bool required = false;
    LayerState state = LayerState::Real;
    std::string identity;   ///< identity profile to adopt when required
    std::string reason;
};

struct Resolution {
    std::string package;
    Mode mode = Mode::Real;
    bool known_game = false;
    Confidence confidence = Confidence::Unknown;
    std::vector<Gate> gates;
    std::vector<LayerDecision> layers;   ///< always all four, in Layer order
    std::vector<Blocker> blockers;
    std::vector<std::string> warnings;

    // The three concepts that must never be collapsed.
    Tri unlocked = Tri::Unknown;   ///< the gate is satisfied by the plan
    Tri capable = Tri::Unknown;    ///< the real hardware can do the target
    Tri sustained = Tri::Unknown;  ///< filled by the runtime from live FPS, never by the resolver

    bool should_apply = false;     ///< any layer actually applied
    std::string recommendation;    ///< e.g. "create_profile" for an unknown game

    std::set<Layer> required_layers() const;
    const LayerDecision &decision(Layer l) const;
    std::string to_json() const;
};

/// Live measurement, folded into a resolution to answer "sustained".
struct LiveSample {
    double fps = 0;
    double target_fps = 0;
    bool valid = false;
};
Tri judge_sustained(const LiveSample &s);

Resolution resolve_compatibility(const EffectiveProfile &profile, const std::optional<GameRequirement> &known,
                                 const RealHardware &hw, const ProfileLibrary &lib);

} // namespace flux::compat
