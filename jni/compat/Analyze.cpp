#include "Analyze.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "CapabilityModel.hpp"
#include "GameProfile.hpp"
#include "Hardware.hpp"
#include "Resolver.hpp"

namespace flux::compat {

namespace {

std::string fail(const std::string &why) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    w.Key("ok"); w.Bool(false);
    w.Key("error"); w.String(why.c_str());
    w.EndObject();
    return sb.GetString();
}

} // namespace

bool build_inputs(const std::string &package, std::optional<Mode> mode_override, Mode unprofiled_mode,
                  const AnalyzeInputs &in, ResolvedInputs &out, std::string &error) {
    if (package.empty() || package.find('/') != std::string::npos || package.find("..") != std::string::npos) {
        error = "invalid package name";
        return false;
    }

    ResolvedInputs r;
    if (!in.capabilities_json.empty()) {
        flux::gfx::CapabilityModel model;
        std::string err;
        if (!flux::gfx::CapabilityModel::from_json(in.capabilities_json, model, err)) {
            error = "capabilities: " + err;
            return false;
        }
        r.hw = hardware_from_model(model);
    }

    if (!in.library_json.empty()) {
        std::string err;
        if (!r.lib.load_json(in.library_json, err)) {
            error = "library: " + err;
            return false;
        }
    }

    if (!in.known_games_json.empty()) {
        KnownGameDb db;
        std::string err;
        if (!db.load_json(in.known_games_json, err)) {
            error = "known games: " + err;
            return false;
        }
        r.known = db.find(package);
    }

    std::optional<GameProfile> game;
    if (!in.profiles_json.empty()) {
        rapidjson::Document d;
        d.Parse(in.profiles_json.c_str());
        if (d.HasParseError() || !d.IsObject()) {
            error = "profiles: invalid JSON";
            return false;
        }
        if (d.HasMember(package.c_str())) {
            rapidjson::StringBuffer sb;
            rapidjson::Writer<rapidjson::StringBuffer> w(sb);
            d[package.c_str()].Accept(w);
            GameProfile p;
            std::string err;
            if (!parse_profile(sb.GetString(), p, err)) {
                error = "profile for " + package + ": " + err;
                return false;
            }
            game = std::move(p);
        }
    }
    r.has_profile = game.has_value();

    GameProfile runtime;
    if (mode_override) runtime.compat.mode = std::string(to_string(*mode_override));

    r.profile = r.lib.resolve(package, GameProfile{}, game, runtime, r.warning);
    if (!game && !mode_override) r.profile.mode = unprofiled_mode;
    out = std::move(r);
    return true;
}

std::vector<std::string> profiled_packages(const std::string &profiles_json) {
    std::vector<std::string> out;
    rapidjson::Document d;
    d.Parse(profiles_json.c_str());
    if (d.HasParseError() || !d.IsObject()) return out;
    for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it) {
        const std::string name = it->name.GetString();
        if (name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos) continue;
        out.push_back(name);
    }
    return out;
}

ArmReport arm_all(Arming &arming, const AnalyzeInputs &in, bool tweaks_disabled) {
    ArmReport rep;
    // Tweaks disabled, or the provider cannot be used any more (opt-in withdrawn, module removed):
    // nothing may stay armed.
    if (tweaks_disabled || arming.backend_state() != BackendState::Available) {
        arming.disarm_all();
        return rep;
    }
    for (const std::string &pkg : profiled_packages(in.profiles_json)) {
        ResolvedInputs ri;
        std::string err;
        if (!build_inputs(pkg, std::nullopt, Mode::Real, in, ri, err)) {
            rep.failed.emplace_back(pkg, err);
            arming.disarm(pkg);
            continue;
        }
        Resolution res = resolve_compatibility(ri.profile, ri.known, ri.hw, ri.lib);
        provider::ProcessScope scope;
        if (ri.profile.process_scope == "all") scope.kind = provider::ProcessScope::Kind::All;
        else if (ri.profile.process_scope == "listed") scope.kind = provider::ProcessScope::Kind::Listed;
        scope.processes = ri.profile.processes;
        std::string tx = arming.arm(res, ri.lib, scope, ri.profile.chain.empty() ? "game" : ri.profile.chain.back(), err);
        if (!tx.empty()) rep.armed.push_back(pkg);
        else if (!err.empty() && res.should_apply) rep.failed.emplace_back(pkg, err);
        else rep.disarmed.push_back(pkg);
    }
    return rep;
}

std::string analyze_package(const std::string &package, std::optional<Mode> mode_override, const AnalyzeInputs &in) {
    ResolvedInputs ri;
    std::string berr;
    if (!build_inputs(package, mode_override, Mode::Auto, in, ri, berr)) return fail(berr);

    Resolution res = resolve_compatibility(ri.profile, ri.known, ri.hw, ri.lib);
    const RealHardware &hw = ri.hw;
    const std::string &err = ri.warning;
    const bool has_profile = ri.has_profile;

    rapidjson::Document out;
    out.SetObject();
    auto &al = out.GetAllocator();
    out.AddMember("ok", true, al);
    out.AddMember("error", rapidjson::Value(err.c_str(), al), al);
    out.AddMember("has_profile", has_profile, al);
    rapidjson::Document r;
    r.Parse(res.to_json().c_str());
    out.AddMember("resolution", r, al);
    rapidjson::Value real(rapidjson::kObjectType);
    real.AddMember("soc", rapidjson::Value(hw.soc.c_str(), al), al);
    real.AddMember("gpu", rapidjson::Value((hw.gpu_vendor + " " + hw.gpu_model).c_str(), al), al);
    real.AddMember("vulkan", rapidjson::Value(to_string(hw.vulkan), al), al);
    real.AddMember("peak_refresh_hz", hw.peak_hz(), al);
    real.AddMember("sdk", hw.sdk, al);
    out.AddMember("real_hardware", real, al);
    rapidjson::Value backends(rapidjson::kObjectType);
    backends.AddMember("native", "available", al);
    backends.AddMember("zygisk", rapidjson::Value(in.provider_state.c_str(), al), al);
    out.AddMember("backends", backends, al);
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    out.Accept(w);
    return sb.GetString();
}

} // namespace flux::compat
