/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ProfileModel.hpp"

#include <algorithm>
#include <set>

#include <rapidjson/document.h>

namespace flux::perf {

namespace {

using rapidjson::Value;

const std::set<std::string> kProfiles = {"performance", "performance_lite", "balance", "powersave"};
const std::set<std::string> kMemory = {"default", "balanced", "gaming", "gaming_plus"};
const std::set<std::string> kTouch = {"default", "balanced", "responsive", "responsive_plus", "competitive"};
const std::set<std::string> kStorage = {"default", "balanced", "gaming"};
const std::set<std::string> kRefresh = {"real", "adaptive", "hz60", "hz90", "hz120", "hz144", "custom"};
constexpr int kMinHz = 30, kMaxHz = 240;
constexpr size_t kMaxName = 64;

struct Ctx {
    LoadResult &res;
    std::string where; // "global", "preset 'x'", "game 'pkg'"
    bool err(const std::string &m) {
        res.errors.push_back(m + " in " + where);
        return false;
    }
    void warn(const std::string &m) { res.warnings.push_back(m + " in " + where); }
};

bool read_enum(Ctx &c, const std::string &key, const Value &v, const std::set<std::string> &allowed,
               std::optional<std::string> &out) {
    if (!v.IsString()) return c.err(key + " must be a string");
    const std::string s = v.GetString();
    if (key == "touch" && s == "custom") { // legacy value, never supported by the planner
        c.warn("touch 'custom' not supported; ignored");
        return true;
    }
    if (!allowed.count(s)) return c.err("invalid value '" + s + "' for " + key);
    out = s;
    return true;
}

/// One performance field. Returns false with an error for an unknown key or a bad value.
bool read_field(Ctx &c, const std::string &k, const Value &v, ProfileLayer &l, const std::string &prefix) {
    if (k == "profile") return read_enum(c, k, v, kProfiles, l.profile);
    if (k == "memory") return read_enum(c, k, v, kMemory, l.memory);
    if (k == "touch") return read_enum(c, k, v, kTouch, l.touch);
    if (k == "storage") return read_enum(c, k, v, kStorage, l.storage);
    if (k == "refresh") return read_enum(c, k, v, kRefresh, l.refresh);
    if (k == "refresh_custom_hz") {
        if (!v.IsInt() || v.GetInt() < kMinHz || v.GetInt() > kMaxHz)
            return c.err("refresh_custom_hz must be an integer " + std::to_string(kMinHz) + ".." +
                         std::to_string(kMaxHz));
        l.refresh_custom_hz = v.GetInt();
        return true;
    }
    if (k == "launch_boost") {
        if (!v.IsBool()) return c.err("launch_boost must be a boolean");
        l.launch_boost = v.GetBool();
        return true;
    }
    return c.err("invalid field '" + prefix + k + "'");
}

/// One layer object. `allow_extends` is false for global and runtime.
std::optional<ProfileLayer> read_layer(Ctx &c, const Value &o, bool allow_extends) {
    if (!o.IsObject()) {
        c.err("entry must be an object");
        return std::nullopt;
    }
    ProfileLayer l;
    for (auto it = o.MemberBegin(); it != o.MemberEnd(); ++it) {
        const std::string k = it->name.GetString();
        if (k == "extends") {
            if (!allow_extends) { c.err("invalid field 'extends'"); return std::nullopt; }
            if (!it->value.IsString() || it->value.GetStringLength() == 0 || it->value.GetStringLength() > kMaxName) {
                c.err("extends must be a non-empty string of at most 64 characters");
                return std::nullopt;
            }
            l.extends = it->value.GetString();
        } else if (k == "performance") { // legacy nesting
            if (!it->value.IsObject()) { c.err("performance must be an object"); return std::nullopt; }
            for (auto f = it->value.MemberBegin(); f != it->value.MemberEnd(); ++f)
                if (!read_field(c, f->name.GetString(), f->value, l, "performance.")) return std::nullopt;
        } else if (k == "compatibility") {
            c.warn("compatibility ignored (identity data is not supported)");
        } else if (k == "package") {
            // legacy duplicate of the key; carries nothing
        } else if (!read_field(c, k, it->value, l, "")) {
            return std::nullopt;
        }
    }
    if (l.refresh && *l.refresh == "custom" && !l.refresh_custom_hz) {
        c.err("refresh 'custom' needs refresh_custom_hz");
        return std::nullopt;
    }
    return l;
}

void read_map(LoadResult &res, const Value &o, const char *kind, std::map<std::string, ProfileLayer> &out) {
    if (!o.IsObject()) {
        res.errors.push_back(std::string(kind) + "s must be an object");
        return;
    }
    for (auto it = o.MemberBegin(); it != o.MemberEnd(); ++it) {
        const std::string name = it->name.GetString();
        Ctx c{res, std::string(kind) + " '" + name + "'"};
        if (name.empty() || name.size() > 256) { c.err("invalid name"); continue; }
        if (auto l = read_layer(c, it->value, true)) out[name] = *l;
    }
}

template <typename T>
void overlay(const std::optional<T> &v, T &dst, const char *field, const FieldSource &src,
             std::map<std::string, FieldSource> &sources) {
    if (!v) return;
    dst = *v;
    sources[field] = src;
}

void apply(const ProfileLayer &l, const FieldSource &src, ResolvedProfile &r) {
    overlay(l.profile, r.profile, "profile", src, r.sources);
    overlay(l.memory, r.perf.memory, "memory", src, r.sources);
    overlay(l.touch, r.perf.touch, "touch", src, r.sources);
    overlay(l.storage, r.perf.storage, "storage", src, r.sources);
    overlay(l.refresh, r.perf.refresh, "refresh", src, r.sources);
    overlay(l.refresh_custom_hz, r.perf.refresh_custom_hz, "refresh_custom_hz", src, r.sources);
    overlay(l.launch_boost, r.perf.launch_boost, "launch_boost", src, r.sources);
    r.chain.push_back(src.name.empty() ? src.layer : src.layer + " " + src.name);
}

constexpr const char *kFields[] = {"profile", "memory", "touch", "storage", "refresh", "refresh_custom_hz", "launch_boost"};

} // namespace

const char *to_string(Format f) {
    switch (f) {
    case Format::Current: return "current";
    case Format::LegacyGameProfiles: return "legacy_game_profiles";
    case Format::LegacyLibrary: return "legacy_library";
    }
    return "current";
}

LoadResult load_document(const std::string &json, ProfileDocument &out) {
    LoadResult res;
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError() || !d.IsObject()) {
        res.errors.push_back("document is not a JSON object");
        return res;
    }
    if (d.HasMember("version")) {
        res.format = Format::Current;
        if (!d["version"].IsInt() || d["version"].GetInt() != 1) {
            res.errors.push_back("unsupported version");
            return res;
        }
        for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it) {
            const std::string k = it->name.GetString();
            if (k == "version") continue;
            if (k == "global") {
                Ctx c{res, "global"};
                if (auto l = read_layer(c, it->value, false)) out.global = *l;
            } else if (k == "presets") read_map(res, it->value, "preset", out.presets);
            else if (k == "games") read_map(res, it->value, "game", out.games);
            else res.errors.push_back("invalid field '" + k + "' in document");
        }
    } else if (d.HasMember("presets") || d.HasMember("identities")) {
        res.format = Format::LegacyLibrary;
        for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it) {
            const std::string k = it->name.GetString();
            if (k == "presets") read_map(res, it->value, "preset", out.presets);
            else if (k == "identities") res.warnings.push_back("identities ignored (identity data is not supported)");
            else res.errors.push_back("invalid field '" + k + "' in document");
        }
    } else {
        res.format = Format::LegacyGameProfiles;
        read_map(res, d, "game", out.games);
    }
    res.ok = res.errors.empty();
    return res;
}

void merge_document(ProfileDocument &into, const ProfileDocument &from) {
    for (const auto &[k, v] : from.presets) into.presets.emplace(k, v);
    for (const auto &[k, v] : from.games) into.games.emplace(k, v);
}

ProfileLayer layer_from_gamelist(bool lite_mode) {
    ProfileLayer l;
    if (lite_mode) l.profile = "performance_lite";
    return l;
}

std::string FieldSource::describe() const {
    if (layer == "builtin") return "builtin default";
    if (layer == "preset") return "preset " + name;
    if (layer == "game") return "game override";
    if (layer == "runtime") return "runtime override";
    return layer;
}

std::string ResolvedProfile::explain() const {
    std::string out;
    for (const char *f : kFields) {
        auto it = sources.find(f);
        out += std::string(f) + ":\n  source=" + (it != sources.end() ? it->second.describe() : "builtin default") + "\n";
    }
    for (const auto &e : errors) out += "error: " + e + "\n";
    return out;
}

ResolvedProfile resolve(const ProfileDocument &doc, const std::string &package,
                        const std::optional<ProfileLayer> &runtime) {
    ResolvedProfile r;
    const FieldSource builtin{"builtin", ""};
    for (const char *f : kFields) r.sources[f] = builtin;
    r.chain.push_back("builtin");

    apply(doc.global, {"global", ""}, r);

    const auto game = doc.games.find(package);
    std::vector<std::string> presets; // child first
    if (game != doc.games.end()) {
        std::string name = game->second.extends, from;
        while (!name.empty()) {
            if (std::find(presets.begin(), presets.end(), name) != presets.end()) {
                std::string cyc;
                for (const auto &p : presets) cyc += p + " -> ";
                r.errors.push_back("inheritance cycle: " + cyc + name);
                break;
            }
            const auto p = doc.presets.find(name);
            if (p == doc.presets.end()) {
                r.errors.push_back(from.empty() ? "unknown profile '" + name + "'"
                                                : "missing parent '" + name + "' of preset '" + from + "'");
                break;
            }
            presets.push_back(name);
            from = name;
            name = p->second.extends;
        }
    }
    for (auto it = presets.rbegin(); it != presets.rend(); ++it) apply(doc.presets.at(*it), {"preset", *it}, r);
    if (game != doc.games.end()) apply(game->second, {"game", package}, r);
    if (runtime) apply(*runtime, {"runtime", ""}, r);
    return r;
}

} // namespace flux::perf
