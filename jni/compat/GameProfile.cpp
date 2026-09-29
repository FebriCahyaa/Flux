#include "GameProfile.hpp"

#include <algorithm>
#include <set>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::compat {

namespace {

const std::set<std::string> kPerf = {"performance", "balance", "powersave"};
const std::set<std::string> kMemory = {"default", "balanced", "gaming", "gaming_plus"};
const std::set<std::string> kTouch = {"default", "balanced", "responsive", "responsive_plus", "competitive", "custom"};
const std::set<std::string> kStorage = {"default", "balanced", "gaming"};
const std::set<std::string> kRefresh = {"real", "adaptive", "hz60", "hz90", "hz120", "custom"};

bool read_enum(const rapidjson::Value &obj, const char *key, const std::set<std::string> &allowed,
               std::optional<std::string> &out, std::string &error) {
    if (!obj.HasMember(key)) return true;
    const auto &v = obj[key];
    if (!v.IsString()) {
        error = std::string(key) + " must be a string";
        return false;
    }
    std::string s = v.GetString();
    if (!allowed.count(s)) {
        error = std::string("unknown value '") + s + "' for " + key;
        return false;
    }
    out = s;
    return true;
}

bool read_name(const rapidjson::Value &obj, const char *key, std::optional<std::string> &out, std::string &error) {
    if (!obj.HasMember(key)) return true;
    const auto &v = obj[key];
    if (!v.IsString() || v.GetStringLength() == 0 || v.GetStringLength() > 64) {
        error = std::string(key) + " must be a non-empty string of at most 64 characters";
        return false;
    }
    out = v.GetString();
    return true;
}

bool parse_value(const rapidjson::Value &o, GameProfile &out, std::string &error) {
    if (!o.IsObject()) {
        error = "profile must be an object";
        return false;
    }
    if (o.HasMember("package")) {
        if (!o["package"].IsString()) { error = "package must be a string"; return false; }
        out.package = o["package"].GetString();
    }
    if (o.HasMember("extends")) {
        if (!o["extends"].IsString()) { error = "extends must be a string"; return false; }
        out.extends = o["extends"].GetString();
    }
    if (o.HasMember("performance")) {
        const auto &p = o["performance"];
        if (!p.IsObject()) { error = "performance must be an object"; return false; }
        if (!read_enum(p, "profile", kPerf, out.perf.profile, error)) return false;
        if (!read_enum(p, "memory", kMemory, out.perf.memory, error)) return false;
        if (!read_enum(p, "touch", kTouch, out.perf.touch, error)) return false;
        if (!read_enum(p, "storage", kStorage, out.perf.storage, error)) return false;
        if (!read_enum(p, "refresh", kRefresh, out.perf.refresh, error)) return false;
        if (p.HasMember("launch_boost")) {
            if (!p["launch_boost"].IsBool()) { error = "launch_boost must be a boolean"; return false; }
            out.perf.launch_boost = p["launch_boost"].GetBool();
        }
    }
    if (o.HasMember("compatibility")) {
        const auto &c = o["compatibility"];
        if (!c.IsObject()) { error = "compatibility must be an object"; return false; }
        if (c.HasMember("mode")) {
            if (!c["mode"].IsString() || !parse_mode(c["mode"].GetString())) {
                error = "unknown compatibility mode";
                return false;
            }
            out.compat.mode = std::string(c["mode"].GetString());
        }
        if (!read_name(c, "device_profile", out.compat.device_profile, error)) return false;
        if (!read_name(c, "cpu_profile", out.compat.cpu_profile, error)) return false;
        if (!read_name(c, "gpu_profile", out.compat.gpu_profile, error)) return false;
        if (!read_name(c, "display_profile", out.compat.display_profile, error)) return false;
        if (c.HasMember("process_scope")) {
            const std::set<std::string> scopes = {"main", "listed", "all"};
            if (!c["process_scope"].IsString() || !scopes.count(c["process_scope"].GetString())) {
                error = "process_scope must be main, listed or all";
                return false;
            }
            out.compat.process_scope = std::string(c["process_scope"].GetString());
        }
        if (c.HasMember("processes")) {
            if (!c["processes"].IsArray() || c["processes"].Size() > 16) {
                error = "processes must be an array of at most 16 names";
                return false;
            }
            std::vector<std::string> names;
            for (const auto &v : c["processes"].GetArray()) {
                if (!v.IsString() || v.GetStringLength() == 0 || v.GetStringLength() > 128) {
                    error = "processes entries must be non-empty strings";
                    return false;
                }
                names.push_back(v.GetString());
            }
            out.compat.processes = std::move(names);
        }
    }
    return true;
}

template <typename T>
void overlay(std::optional<T> src, T &dst) {
    if (src) dst = *src;
}

void apply_layer(const GameProfile &l, EffectiveProfile &e) {
    overlay(l.perf.profile, e.performance);
    overlay(l.perf.memory, e.memory);
    overlay(l.perf.touch, e.touch);
    overlay(l.perf.storage, e.storage);
    overlay(l.perf.refresh, e.refresh);
    overlay(l.perf.launch_boost, e.launch_boost);
    if (l.compat.mode)
        if (auto m = parse_mode(*l.compat.mode)) e.mode = *m;
    overlay(l.compat.device_profile, e.device_profile);
    overlay(l.compat.cpu_profile, e.cpu_profile);
    overlay(l.compat.gpu_profile, e.gpu_profile);
    overlay(l.compat.display_profile, e.display_profile);
    overlay(l.compat.process_scope, e.process_scope);
    if (l.compat.processes) e.processes = *l.compat.processes;
}

void put_opt(rapidjson::Writer<rapidjson::StringBuffer> &w, const char *k, const std::optional<std::string> &v) {
    if (!v) return;
    w.Key(k);
    w.String(v->c_str());
}

void write_profile(rapidjson::Writer<rapidjson::StringBuffer> &w, const GameProfile &p) {
    w.StartObject();
    if (!p.package.empty()) { w.Key("package"); w.String(p.package.c_str()); }
    if (!p.extends.empty()) { w.Key("extends"); w.String(p.extends.c_str()); }
    w.Key("performance");
    w.StartObject();
    put_opt(w, "profile", p.perf.profile);
    put_opt(w, "memory", p.perf.memory);
    put_opt(w, "touch", p.perf.touch);
    put_opt(w, "storage", p.perf.storage);
    put_opt(w, "refresh", p.perf.refresh);
    if (p.perf.launch_boost) { w.Key("launch_boost"); w.Bool(*p.perf.launch_boost); }
    w.EndObject();
    w.Key("compatibility");
    w.StartObject();
    put_opt(w, "mode", p.compat.mode);
    put_opt(w, "device_profile", p.compat.device_profile);
    put_opt(w, "cpu_profile", p.compat.cpu_profile);
    put_opt(w, "gpu_profile", p.compat.gpu_profile);
    put_opt(w, "display_profile", p.compat.display_profile);
    put_opt(w, "process_scope", p.compat.process_scope);
    if (p.compat.processes) {
        w.Key("processes");
        w.StartArray();
        for (const auto &n : *p.compat.processes) w.String(n.c_str());
        w.EndArray();
    }
    w.EndObject();
    w.EndObject();
}

// Key fragments that describe capability rather than identity.
bool claims_capability(Layer layer, const std::string &key) {
    static const char *cpu[] = {"feature", "hwcap", "isa", "neon", "sve", "asimd", "crypto", "instruction"};
    static const char *gpu[] = {"extension", "feature", "limit", "capabil", "texture", "shader"};
    std::string k = key;
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
    if (layer == Layer::Cpu)
        for (const char *f : cpu) if (k.find(f) != std::string::npos) return true;
    if (layer == Layer::Gpu)
        for (const char *f : gpu) if (k.find(f) != std::string::npos) return true;
    return false;
}

} // namespace

bool parse_profile(const std::string &json, GameProfile &out, std::string &error) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError()) {
        error = "invalid JSON";
        return false;
    }
    GameProfile p;
    if (!parse_value(d, p, error)) return false;
    out = std::move(p);
    return true;
}

std::string profile_to_json(const GameProfile &p) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    write_profile(w, p);
    return sb.GetString();
}

bool validate_identity(const IdentityProfile &id, std::string &error) {
    if (id.name.empty()) {
        error = "identity has no name";
        return false;
    }
    if (id.layer == Layer::Display) {
        error = "display compatibility is a refresh setting, not an identity";
        return false;
    }
    for (const auto &[k, v] : id.fields) {
        if (claims_capability(id.layer, k)) {
            error = "identity field '" + k + "' would advertise a hardware capability";
            return false;
        }
        (void)v;
    }
    return true;
}

bool ProfileLibrary::load_json(const std::string &text, std::string &error) {
    rapidjson::Document d;
    d.Parse(text.c_str());
    if (d.HasParseError() || !d.IsObject()) {
        error = "invalid library JSON";
        return false;
    }
    std::map<std::string, GameProfile> new_presets;
    std::map<std::string, IdentityProfile> new_ids;

    if (d.HasMember("presets")) {
        if (!d["presets"].IsObject()) { error = "presets must be an object"; return false; }
        for (auto it = d["presets"].MemberBegin(); it != d["presets"].MemberEnd(); ++it) {
            GameProfile p;
            std::string e;
            if (!parse_value(it->value, p, e)) { error = std::string("preset '") + it->name.GetString() + "': " + e; return false; }
            new_presets[it->name.GetString()] = std::move(p);
        }
    }
    if (d.HasMember("identities")) {
        if (!d["identities"].IsObject()) { error = "identities must be an object"; return false; }
        for (auto it = d["identities"].MemberBegin(); it != d["identities"].MemberEnd(); ++it) {
            const auto &o = it->value;
            IdentityProfile id;
            id.name = it->name.GetString();
            if (!o.IsObject() || !o.HasMember("layer") || !o["layer"].IsString()) {
                error = "identity '" + id.name + "' needs a layer";
                return false;
            }
            std::string layer = o["layer"].GetString();
            if (layer == "device") id.layer = Layer::Device;
            else if (layer == "cpu") id.layer = Layer::Cpu;
            else if (layer == "gpu") id.layer = Layer::Gpu;
            else { error = "identity '" + id.name + "' has unknown layer '" + layer + "'"; return false; }
            if (o.HasMember("fields") && o["fields"].IsObject())
                for (auto f = o["fields"].MemberBegin(); f != o["fields"].MemberEnd(); ++f)
                    if (f->value.IsString()) id.fields[f->name.GetString()] = f->value.GetString();
            std::string e;
            if (!validate_identity(id, e)) { error = "identity '" + id.name + "': " + e; return false; }
            new_ids[id.name] = std::move(id);
        }
    }
    presets = std::move(new_presets);
    identities = std::move(new_ids);
    return true;
}

std::string ProfileLibrary::to_json() const {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    w.Key("presets");
    w.StartObject();
    for (const auto &[name, p] : presets) { w.Key(name.c_str()); write_profile(w, p); }
    w.EndObject();
    w.Key("identities");
    w.StartObject();
    for (const auto &[name, id] : identities) {
        w.Key(name.c_str());
        w.StartObject();
        w.Key("layer"); w.String(to_string(id.layer));
        w.Key("fields");
        w.StartObject();
        for (const auto &[k, v] : id.fields) { w.Key(k.c_str()); w.String(v.c_str()); }
        w.EndObject();
        w.EndObject();
    }
    w.EndObject();
    w.EndObject();
    return sb.GetString();
}

EffectiveProfile ProfileLibrary::resolve(const std::string &package, const GameProfile &global,
                                         const std::optional<GameProfile> &game, const GameProfile &runtime,
                                         std::string &error) const {
    EffectiveProfile e;
    e.package = package;
    apply_layer(global, e);
    e.chain.push_back("global");

    if (game) {
        // Walk the extends chain outward first, then apply innermost-last so the
        // game's own values win over every preset it inherits from.
        std::vector<const GameProfile *> stack;
        std::set<std::string> seen;
        std::string next = game->extends;
        while (!next.empty()) {
            if (!seen.insert(next).second) { error = "preset cycle at '" + next + "'"; break; }
            auto it = presets.find(next);
            if (it == presets.end()) { error = "unknown preset '" + next + "'"; break; }
            stack.push_back(&it->second);
            next = it->second.extends;
        }
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            apply_layer(**it, e);
            e.chain.push_back("preset");
        }
        apply_layer(*game, e);
        e.chain.push_back("game");
    }
    apply_layer(runtime, e);
    e.chain.push_back("runtime");
    return e;
}

} // namespace flux::compat
