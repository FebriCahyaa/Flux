#include "Resolver.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::compat {

namespace {

constexpr Layer kLayers[] = {Layer::Device, Layer::Cpu, Layer::Gpu, Layer::Display};

std::optional<Layer> layer_for(GateKind k) {
    switch (k) {
    case GateKind::DeviceIdentity: return Layer::Device;
    case GateKind::CpuIdentity: return Layer::Cpu;
    case GateKind::GpuIdentity: return Layer::Gpu;
    case GateKind::DisplayRefresh: return Layer::Display;
    default: return std::nullopt;
    }
}

const std::string &profile_for(const EffectiveProfile &p, Layer l) {
    switch (l) {
    case Layer::Device: return p.device_profile;
    case Layer::Cpu: return p.cpu_profile;
    case Layer::Gpu: return p.gpu_profile;
    case Layer::Display: return p.display_profile;
    }
    return p.device_profile;
}

bool is_real(const std::string &name) { return name.rfind("real_", 0) == 0 || name.empty(); }

LayerDecision &at(std::vector<LayerDecision> &v, Layer l) { return v[static_cast<size_t>(l)]; }

/// Can the real hardware reach the target? Only what we can actually read counts.
Tri judge_capable(const GameRequirement &req, const RealHardware &hw, std::string &why) {
    Tri result = Tri::Yes;
    bool any = false;
    if (req.target_fps > 0) {
        any = true;
        Tri panel = hw.panel_supports(req.target_fps);
        if (panel == Tri::No) {
            why = "panel peaks below the target refresh";
            return Tri::No;
        }
        if (panel == Tri::Unknown) result = Tri::Unknown;
    }
    if (req.needs_vulkan) {
        any = true;
        if (hw.vulkan == Tri::No) {
            why = "Vulkan is not available on this device";
            return Tri::No;
        }
        if (hw.vulkan == Tri::Unknown) result = Tri::Unknown;
    }
    return any ? result : Tri::Unknown;
}

} // namespace

std::set<Layer> Resolution::required_layers() const {
    std::set<Layer> s;
    for (const auto &d : layers)
        if (d.required) s.insert(d.layer);
    return s;
}

const LayerDecision &Resolution::decision(Layer l) const { return layers[static_cast<size_t>(l)]; }

bool KnownGameDb::load_json(const std::string &text, std::string &error) {
    rapidjson::Document d;
    d.Parse(text.c_str());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("games") || !d["games"].IsObject()) {
        error = "expected {\"games\":{...}}";
        return false;
    }
    std::map<std::string, GameRequirement> parsed;
    for (auto it = d["games"].MemberBegin(); it != d["games"].MemberEnd(); ++it) {
        const auto &o = it->value;
        GameRequirement r;
        r.package = it->name.GetString();
        r.known = true;
        r.confidence = Confidence::High;
        if (!o.IsObject()) { error = r.package + ": entry must be an object"; return false; }
        if (o.HasMember("confidence") && o["confidence"].IsString()) {
            std::string c = o["confidence"].GetString();
            if (c == "high") r.confidence = Confidence::High;
            else if (c == "medium") r.confidence = Confidence::Medium;
            else if (c == "low") r.confidence = Confidence::Low;
            else { error = r.package + ": unknown confidence '" + c + "'"; return false; }
        }
        if (o.HasMember("target_fps") && o["target_fps"].IsNumber()) r.target_fps = o["target_fps"].GetDouble();
        if (o.HasMember("needs_vulkan") && o["needs_vulkan"].IsBool()) r.needs_vulkan = o["needs_vulkan"].GetBool();
        if (o.HasMember("gates")) {
            if (!o["gates"].IsArray()) { error = r.package + ": gates must be an array"; return false; }
            for (const auto &g : o["gates"].GetArray()) {
                Gate gate;
                std::string kind = g.IsString() ? g.GetString()
                                   : (g.IsObject() && g.HasMember("kind") && g["kind"].IsString()) ? g["kind"].GetString() : "";
                auto k = parse_gate(kind);
                if (!k) { error = r.package + ": unknown gate '" + kind + "'"; return false; }
                gate.kind = *k;
                if (g.IsObject() && g.HasMember("detail") && g["detail"].IsString()) gate.detail = g["detail"].GetString();
                if (g.IsObject() && g.HasMember("identity") && g["identity"].IsString()) gate.identity = g["identity"].GetString();
                r.gates.push_back(gate);
            }
        }
        parsed[r.package] = std::move(r);
    }
    games_ = std::move(parsed);
    return true;
}

std::optional<GameRequirement> KnownGameDb::find(const std::string &package) const {
    auto it = games_.find(package);
    if (it == games_.end()) return std::nullopt;
    return it->second;
}

Tri judge_sustained(const LiveSample &s) {
    if (!s.valid || s.target_fps <= 0) return Tri::Unknown;
    // Within 5% of the target counts as holding it; anything else is not sustained.
    return s.fps >= s.target_fps * 0.95 ? Tri::Yes : Tri::No;
}

Resolution resolve_compatibility(const EffectiveProfile &profile, const std::optional<GameRequirement> &known,
                                 const RealHardware &hw, const ProfileLibrary &lib) {
    Resolution r;
    r.package = profile.package;
    r.mode = profile.mode;
    r.known_game = known.has_value() && known->known;
    r.confidence = known ? known->confidence : Confidence::Unknown;
    if (known) r.gates = known->gates;

    r.layers.resize(4);
    for (Layer l : kLayers) {
        auto &d = at(r.layers, l);
        d.layer = l;
        d.state = LayerState::Real;
        d.reason = "not required";
    }

    GameRequirement req = known.value_or(GameRequirement{});
    std::string why_incapable;
    r.capable = judge_capable(req, hw, why_incapable);

    // -- which layers does this mode want? ---------------------------------
    std::set<Layer> wanted;
    if (profile.mode == Mode::Real) {
        for (Layer l : kLayers) at(r.layers, l).reason = "mode is real";
    } else if (profile.mode == Mode::Auto || profile.mode == Mode::Compatibility) {
        bool gate_known = false;
        for (const Gate &g : r.gates) {
            if (g.kind == GateKind::None) { gate_known = true; continue; }
            if (g.kind == GateKind::Unknown) continue;
            gate_known = true;
            if (auto l = layer_for(g.kind)) {
                wanted.insert(*l);
            } else {
                r.blockers.push_back({g.kind, g.kind == GateKind::HardwareInsufficient
                                                  ? "the hardware cannot provide what the game requires"
                                                  : "the gate is outside anything Flux can change locally"});
            }
        }
        if (!gate_known) {
            r.recommendation = "create_profile";
            r.warnings.push_back(r.known_game ? "known game without a decisive gate" : "no reliable profile for this game");
            for (Layer l : kLayers) {
                auto &d = at(r.layers, l);
                d.state = LayerState::Unknown;
                d.reason = "gate unknown; nothing is overridden without evidence";
            }
        }
    } else { // Advanced / Custom: the user named the layers explicitly
        for (Layer l : kLayers)
            if (!is_real(profile_for(profile, l))) wanted.insert(l);
    }

    // -- honesty checks before anything is applied -------------------------
    bool hw_blocks = r.capable == Tri::No;
    if (hw_blocks) r.blockers.push_back({GateKind::HardwareInsufficient, why_incapable});
    if (hw_blocks && profile.mode == Mode::Auto) {
        // Auto never applies an unlock the hardware cannot back up.
        for (Layer l : wanted) {
            auto &d = at(r.layers, l);
            d.state = LayerState::Unsupported;
            d.reason = "skipped: " + why_incapable;
        }
        wanted.clear();
    } else if (hw_blocks) {
        r.warnings.push_back("applied on request, but the hardware is not capable: " + why_incapable);
    }

    // -- turn wanted layers into concrete identities -----------------------
    for (Layer l : wanted) {
        auto &d = at(r.layers, l);
        d.required = true;

        if (l == Layer::Display) {
            if (req.target_fps > 0 && hw.panel_supports(req.target_fps) == Tri::No) {
                d.required = false;
                d.state = LayerState::Unsupported;
                d.reason = "the panel has no mode at the target refresh";
                continue;
            }
            d.state = LayerState::Available;
            d.reason = "game gates on refresh rate; the panel offers the mode";
            continue;
        }

        std::string name = profile_for(profile, l);
        if (is_real(name) && profile.mode == Mode::Auto) {
            // Auto: adopt the identity the curated entry says satisfies this gate.
            for (const Gate &g : r.gates)
                if (layer_for(g.kind) == l && !g.identity.empty()) name = g.identity;
        }
        if (is_real(name)) {
            // Auto found the gate but the user hasn't picked an identity to satisfy it.
            d.state = LayerState::Available;
            d.reason = "gate detected; no identity profile selected for this layer";
            d.required = false;
            r.warnings.push_back(std::string("select a ") + to_string(l) + " identity profile to satisfy the gate");
            continue;
        }
        auto it = lib.identities.find(name);
        if (it == lib.identities.end() || it->second.layer != l) {
            d.required = false;
            d.state = LayerState::Unsupported;
            d.reason = "identity profile '" + name + "' is missing or belongs to another layer";
            continue;
        }
        d.identity = name;
        d.state = LayerState::Available;
        d.reason = "required by " + std::string(profile.mode == Mode::Auto ? "the detected gate" : "the selected profile");
    }

    bool any_required = false;
    for (const auto &d : r.layers) any_required = any_required || d.required;
    r.should_apply = any_required;

    // -- unlocked: is every local gate covered by a required layer? --------
    bool any_local_gate = false, all_covered = true;
    for (const Gate &g : r.gates) {
        auto l = layer_for(g.kind);
        if (!l) continue;
        any_local_gate = true;
        if (!at(r.layers, *l).required) all_covered = false;
    }
    if (!r.blockers.empty() && !hw_blocks) all_covered = false;
    r.unlocked = !any_local_gate ? Tri::Unknown : (all_covered && r.blockers.empty() ? Tri::Yes : Tri::No);
    // (unlocked is only ever a statement about the gate; capable is set above.)
    return r;
}

std::string Resolution::to_json() const {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    w.Key("package"); w.String(package.c_str());
    w.Key("mode"); w.String(to_string(mode));
    w.Key("known_game"); w.Bool(known_game);
    w.Key("confidence"); w.String(to_string(confidence));
    w.Key("unlocked"); w.String(to_string(unlocked));
    w.Key("capable"); w.String(to_string(capable));
    w.Key("sustained"); w.String(to_string(sustained));
    w.Key("should_apply"); w.Bool(should_apply);
    w.Key("recommendation"); w.String(recommendation.c_str());
    w.Key("gates");
    w.StartArray();
    for (const auto &g : gates) {
        w.StartObject();
        w.Key("kind"); w.String(to_string(g.kind));
        w.Key("detail"); w.String(g.detail.c_str());
        w.EndObject();
    }
    w.EndArray();
    w.Key("layers");
    w.StartArray();
    for (const auto &d : layers) {
        w.StartObject();
        w.Key("layer"); w.String(to_string(d.layer));
        w.Key("required"); w.Bool(d.required);
        w.Key("state"); w.String(to_string(d.state));
        w.Key("identity"); w.String(d.identity.c_str());
        w.Key("reason"); w.String(d.reason.c_str());
        w.EndObject();
    }
    w.EndArray();
    w.Key("blockers");
    w.StartArray();
    for (const auto &b : blockers) {
        w.StartObject();
        w.Key("kind"); w.String(to_string(b.kind));
        w.Key("reason"); w.String(b.reason.c_str());
        w.EndObject();
    }
    w.EndArray();
    w.Key("warnings");
    w.StartArray();
    for (const auto &s : warnings) w.String(s.c_str());
    w.EndArray();
    w.EndObject();
    return sb.GetString();
}

} // namespace flux::compat
