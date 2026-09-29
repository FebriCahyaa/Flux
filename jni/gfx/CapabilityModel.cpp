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

#include "CapabilityModel.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::gfx {

namespace {

/// Key under which a domain's default source is recorded in the provenance object.
constexpr const char *kDomainSourceKey = "_source";

template <typename Writer>
void write_value(Writer &w, const CapabilityValue &v) {
    std::visit(
        [&w](const auto &held) {
            using T = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<T, bool>) {
                w.Bool(held);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                w.Int64(held);
            } else if constexpr (std::is_same_v<T, double>) {
                w.Double(held);
            } else if constexpr (std::is_same_v<T, std::string>) {
                w.String(held.c_str(), static_cast<rapidjson::SizeType>(held.size()));
            } else {
                w.StartArray();
                for (const auto &s : held) w.String(s.c_str(), static_cast<rapidjson::SizeType>(s.size()));
                w.EndArray();
            }
        },
        v);
}

/// Convert a parsed JSON value back into a CapabilityValue, or nullopt for
/// shapes the model does not represent (null, object, mixed array).
std::optional<CapabilityValue> read_value(const rapidjson::Value &v) {
    if (v.IsBool()) return CapabilityValue{v.GetBool()};
    if (v.IsInt64()) return CapabilityValue{v.GetInt64()};
    if (v.IsDouble()) return CapabilityValue{v.GetDouble()};
    if (v.IsString()) return CapabilityValue{std::string(v.GetString(), v.GetStringLength())};
    if (v.IsArray()) {
        std::vector<std::string> list;
        for (const auto &item : v.GetArray()) {
            if (!item.IsString()) return std::nullopt;
            list.emplace_back(item.GetString(), item.GetStringLength());
        }
        return CapabilityValue{std::move(list)};
    }
    return std::nullopt;
}

template <typename Writer>
void write_model(Writer &w, const CapabilityModel &m) {
    w.StartObject();
    w.Key("schema_version");
    w.Int(m.schema_version);
    w.Key("generated_at_ms");
    w.Int64(m.generated_at_ms);

    w.Key("capabilities");
    w.StartObject();
    for (const auto &[name, dom] : m.domains()) {
        w.Key(name.c_str());
        w.StartObject();
        for (const auto &[key, value] : dom.values) {
            w.Key(key.c_str());
            write_value(w, value);
        }
        w.EndObject();
    }
    w.EndObject();

    w.Key("provenance");
    w.StartObject();
    for (const auto &[name, dom] : m.domains()) {
        w.Key(name.c_str());
        w.StartObject();
        w.Key(kDomainSourceKey);
        w.String(dom.source.c_str());
        for (const auto &[key, src] : dom.value_source) {
            w.Key(key.c_str());
            w.String(src.c_str());
        }
        w.EndObject();
    }
    w.EndObject();

    w.Key("sources");
    w.StartArray();
    for (const auto &s : m.sources()) {
        w.StartObject();
        w.Key("id");
        w.String(s.id.c_str());
        w.Key("domains");
        w.StartArray();
        for (const auto &d : s.domains) w.String(d.c_str());
        w.EndArray();
        w.Key("status");
        w.String(s.status.c_str());
        w.Key("detail");
        w.String(s.detail.c_str());
        w.Key("collected_at_ms");
        w.Int64(s.collected_at_ms);
        w.EndObject();
    }
    w.EndArray();

    w.EndObject();
}

} // namespace

std::string CapabilityDomain::source_of(const std::string &key) const {
    auto it = value_source.find(key);
    return it != value_source.end() ? it->second : source;
}

void CapabilityModel::set(const std::string &domain, const std::string &key, CapabilityValue value,
                          const std::string &source) {
    CapabilityDomain &dom = domains_[domain];
    dom.values[key] = std::move(value);

    if (dom.source.empty()) {
        // First writer sets the domain default; no per-key entry needed.
        dom.source = source;
        dom.value_source.erase(key);
    } else if (dom.source == source) {
        dom.value_source.erase(key);
    } else {
        dom.value_source[key] = source;
    }
}

void CapabilityModel::add_source(const CapabilitySource &source) {
    auto it = std::find_if(sources_.begin(), sources_.end(),
                           [&](const CapabilitySource &s) { return s.id == source.id; });
    if (it != sources_.end()) {
        *it = source;
        return;
    }
    sources_.push_back(source);
}

size_t CapabilityModel::merge(const CapabilityModel &other) {
    size_t taken = 0;
    for (const auto &[name, dom] : other.domains_) {
        for (const auto &[key, value] : dom.values) {
            if (has(name, key)) continue;
            set(name, key, value, dom.source_of(key));
            ++taken;
        }
    }
    for (const auto &s : other.sources_) add_source(s);
    return taken;
}

bool CapabilityModel::has(const std::string &domain, const std::string &key) const {
    return find(domain, key) != nullptr;
}

const CapabilityValue *CapabilityModel::find(const std::string &domain, const std::string &key) const {
    auto dom = domains_.find(domain);
    if (dom == domains_.end()) return nullptr;
    auto it = dom->second.values.find(key);
    return it != dom->second.values.end() ? &it->second : nullptr;
}

std::optional<bool> CapabilityModel::get_bool(const std::string &domain, const std::string &key) const {
    const CapabilityValue *v = find(domain, key);
    if (!v) return std::nullopt;
    if (const bool *b = std::get_if<bool>(v)) return *b;
    return std::nullopt;
}

std::optional<int64_t> CapabilityModel::get_int(const std::string &domain, const std::string &key) const {
    const CapabilityValue *v = find(domain, key);
    if (!v) return std::nullopt;
    if (const int64_t *i = std::get_if<int64_t>(v)) return *i;
    return std::nullopt;
}

std::optional<double> CapabilityModel::get_double(const std::string &domain, const std::string &key) const {
    const CapabilityValue *v = find(domain, key);
    if (!v) return std::nullopt;
    if (const double *d = std::get_if<double>(v)) return *d;
    // An integral value read back from JSON is a legitimate double observation.
    if (const int64_t *i = std::get_if<int64_t>(v)) return static_cast<double>(*i);
    return std::nullopt;
}

std::optional<std::string> CapabilityModel::get_string(const std::string &domain, const std::string &key) const {
    const CapabilityValue *v = find(domain, key);
    if (!v) return std::nullopt;
    if (const std::string *s = std::get_if<std::string>(v)) return *s;
    return std::nullopt;
}

std::optional<std::vector<std::string>> CapabilityModel::get_list(const std::string &domain,
                                                                  const std::string &key) const {
    const CapabilityValue *v = find(domain, key);
    if (!v) return std::nullopt;
    if (const auto *l = std::get_if<std::vector<std::string>>(v)) return *l;
    return std::nullopt;
}

std::optional<std::string> CapabilityModel::source_of(const std::string &domain, const std::string &key) const {
    auto dom = domains_.find(domain);
    if (dom == domains_.end()) return std::nullopt;
    if (dom->second.values.find(key) == dom->second.values.end()) return std::nullopt;
    return dom->second.source_of(key);
}

size_t CapabilityModel::fact_count() const {
    size_t n = 0;
    for (const auto &[name, dom] : domains_) n += dom.values.size();
    return n;
}

std::string CapabilityModel::to_json(bool pretty) const {
    rapidjson::StringBuffer buf;
    if (pretty) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buf);
        w.SetIndent(' ', 2);
        write_model(w, *this);
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        write_model(w, *this);
    }
    return std::string(buf.GetString(), buf.GetSize());
}

bool CapabilityModel::from_json(const std::string &text, CapabilityModel &out, std::string &error) {
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (doc.HasParseError()) {
        error = "malformed JSON at offset " + std::to_string(doc.GetErrorOffset());
        return false;
    }
    if (!doc.IsObject()) {
        error = "root is not an object";
        return false;
    }

    out = CapabilityModel{};

    if (doc.HasMember("schema_version") && doc["schema_version"].IsInt()) {
        out.schema_version = doc["schema_version"].GetInt();
    } else {
        error = "missing schema_version";
        return false;
    }
    if (doc.HasMember("generated_at_ms") && doc["generated_at_ms"].IsInt64()) {
        out.generated_at_ms = doc["generated_at_ms"].GetInt64();
    }

    // Provenance is read first so each fact can be credited as it is stored.
    std::map<std::string, std::string> domain_source;
    std::map<std::string, std::map<std::string, std::string>> key_source;
    if (doc.HasMember("provenance") && doc["provenance"].IsObject()) {
        for (const auto &dm : doc["provenance"].GetObject()) {
            if (!dm.value.IsObject()) continue;
            const std::string dname = dm.name.GetString();
            for (const auto &km : dm.value.GetObject()) {
                if (!km.value.IsString()) continue;
                const std::string kname = km.name.GetString();
                if (kname == kDomainSourceKey) {
                    domain_source[dname] = km.value.GetString();
                } else {
                    key_source[dname][kname] = km.value.GetString();
                }
            }
        }
    }

    if (doc.HasMember("capabilities") && doc["capabilities"].IsObject()) {
        for (const auto &dm : doc["capabilities"].GetObject()) {
            if (!dm.value.IsObject()) continue;
            const std::string dname = dm.name.GetString();
            const std::string dsrc = domain_source.count(dname) ? domain_source[dname] : std::string();

            // Seed the domain source even when every key carries an override, so a
            // round trip preserves which collector owned the domain.
            if (!dsrc.empty()) out.domains_[dname].source = dsrc;

            for (const auto &km : dm.value.GetObject()) {
                const std::string kname = km.name.GetString();
                auto value = read_value(km.value);
                if (!value) continue; // shape the model cannot hold; drop rather than guess
                const auto &ks = key_source[dname];
                auto ksit = ks.find(kname);
                out.set(dname, kname, std::move(*value), ksit != ks.end() ? ksit->second : dsrc);
            }
        }
    }

    if (doc.HasMember("sources") && doc["sources"].IsArray()) {
        for (const auto &sv : doc["sources"].GetArray()) {
            if (!sv.IsObject()) continue;
            CapabilitySource s;
            if (sv.HasMember("id") && sv["id"].IsString()) s.id = sv["id"].GetString();
            if (s.id.empty()) continue;
            if (sv.HasMember("status") && sv["status"].IsString()) s.status = sv["status"].GetString();
            if (sv.HasMember("detail") && sv["detail"].IsString()) s.detail = sv["detail"].GetString();
            if (sv.HasMember("collected_at_ms") && sv["collected_at_ms"].IsInt64()) {
                s.collected_at_ms = sv["collected_at_ms"].GetInt64();
            }
            if (sv.HasMember("domains") && sv["domains"].IsArray()) {
                for (const auto &d : sv["domains"].GetArray()) {
                    if (d.IsString()) s.domains.emplace_back(d.GetString());
                }
            }
            out.add_source(s);
        }
    }

    return true;
}

bool CapabilityModel::write_file(const std::string &path, bool pretty) const {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open()) return false;
        f << to_json(pretty) << '\n';
        if (!f.good()) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool CapabilityModel::read_file(const std::string &path, CapabilityModel &out, std::string &error) {
    std::ifstream f(path);
    if (!f.is_open()) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return from_json(ss.str(), out, error);
}

} // namespace flux::gfx
