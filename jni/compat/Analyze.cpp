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

std::string analyze_package(const std::string &package, std::optional<Mode> mode_override, const AnalyzeInputs &in) {
    if (package.empty() || package.find('/') != std::string::npos || package.find("..") != std::string::npos)
        return fail("invalid package name");

    RealHardware hw; // all-unknown unless a model was supplied
    if (!in.capabilities_json.empty()) {
        flux::gfx::CapabilityModel model;
        std::string err;
        if (!flux::gfx::CapabilityModel::from_json(in.capabilities_json, model, err)) return fail("capabilities: " + err);
        hw = hardware_from_model(model);
    }

    ProfileLibrary lib;
    if (!in.library_json.empty()) {
        std::string err;
        if (!lib.load_json(in.library_json, err)) return fail("library: " + err);
    }

    std::optional<GameRequirement> known;
    if (!in.known_games_json.empty()) {
        KnownGameDb db;
        std::string err;
        if (!db.load_json(in.known_games_json, err)) return fail("known games: " + err);
        known = db.find(package);
    }

    std::optional<GameProfile> game;
    if (!in.profiles_json.empty()) {
        rapidjson::Document d;
        d.Parse(in.profiles_json.c_str());
        if (d.HasParseError() || !d.IsObject()) return fail("profiles: invalid JSON");
        if (d.HasMember(package.c_str())) {
            rapidjson::StringBuffer sb;
            rapidjson::Writer<rapidjson::StringBuffer> w(sb);
            d[package.c_str()].Accept(w);
            GameProfile p;
            std::string err;
            if (!parse_profile(sb.GetString(), p, err)) return fail("profile for " + package + ": " + err);
            game = std::move(p);
        }
    }

    GameProfile runtime;
    if (mode_override) runtime.compat.mode = std::string(to_string(*mode_override));

    std::string err;
    EffectiveProfile eff = lib.resolve(package, GameProfile{}, game, runtime, err);
    if (!game && !mode_override) eff.mode = Mode::Auto; // an unprofiled game is analysed, not left blind
    Resolution res = resolve_compatibility(eff, known, hw, lib);

    rapidjson::Document out;
    out.SetObject();
    auto &al = out.GetAllocator();
    out.AddMember("ok", true, al);
    out.AddMember("error", rapidjson::Value(err.c_str(), al), al);
    out.AddMember("has_profile", game.has_value(), al);
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
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    out.Accept(w);
    return sb.GetString();
}

} // namespace flux::compat
