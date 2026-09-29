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

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "CapabilitySchema.hpp"

namespace flux::gfx {

/**
 * @brief One observed fact.
 *
 * The five alternatives are the value types the canonical schema admits. A fact
 * that could not be observed is not stored at all: absent means "unsupported or
 * not observed", never "false" or "zero".
 */
using CapabilityValue = std::variant<bool, int64_t, double, std::string, std::vector<std::string>>;

/**
 * @brief The facts of one domain, plus where they came from.
 *
 * `source` names the collector that populated the domain. When a domain is merged
 * from several collectors, `value_source` carries a per-key override for the keys
 * that did not come from `source`; keys absent from that map belong to `source`.
 */
struct CapabilityDomain {
    std::string source;
    std::map<std::string, CapabilityValue> values;
    std::map<std::string, std::string> value_source;

    /// Source id credited with @p key, following the per-key override.
    std::string source_of(const std::string &key) const;
};

/** @brief A collector's own run: what it covered and how it went. */
struct CapabilitySource {
    std::string id;                    ///< e.g. "fluxd.vulkan", "synthesiscore.display"
    std::vector<std::string> domains;  ///< domains this collector contributed to
    std::string status = schema::source_status::kOk;
    std::string detail;                ///< why, when status is not ok
    int64_t collected_at_ms = 0;
};

/**
 * @brief The canonical capability model (schema v4).
 *
 * Collectors write facts in; consumers read facts out. The model itself holds no
 * policy: it never ranks, scores or selects. Domain and key names come from
 * CapabilitySchema.hpp, which mirrors SynthesisCore's authoritative descriptor.
 *
 * Ordered containers keep serialisation deterministic, so the JSON is diffable and
 * directly comparable in tests.
 */
class CapabilityModel {
public:
    int schema_version = schema::kVersion;
    int64_t generated_at_ms = 0;

    // -- writing -----------------------------------------------------------

    /// Store @p value under @p domain / @p key, crediting @p source.
    void set(const std::string &domain, const std::string &key, CapabilityValue value,
             const std::string &source);

    /// Record a collector's run. Replaces an earlier entry with the same id.
    void add_source(const CapabilitySource &source);

    /**
     * @brief Fold @p other into this model.
     *
     * Facts already present are kept: the first collector to observe a key owns it,
     * so a later contributor cannot silently overwrite a better observation. Sources
     * from @p other are appended. Returns the number of keys actually taken.
     */
    size_t merge(const CapabilityModel &other);

    // -- reading -----------------------------------------------------------

    bool has(const std::string &domain, const std::string &key) const;
    const CapabilityValue *find(const std::string &domain, const std::string &key) const;

    /// Typed accessors; return nullopt when absent or stored as another type.
    std::optional<bool> get_bool(const std::string &domain, const std::string &key) const;
    std::optional<int64_t> get_int(const std::string &domain, const std::string &key) const;
    std::optional<double> get_double(const std::string &domain, const std::string &key) const;
    std::optional<std::string> get_string(const std::string &domain, const std::string &key) const;
    std::optional<std::vector<std::string>> get_list(const std::string &domain, const std::string &key) const;

    /// Source id credited with a key, or nullopt when the key is absent.
    std::optional<std::string> source_of(const std::string &domain, const std::string &key) const;

    const std::map<std::string, CapabilityDomain> &domains() const { return domains_; }
    const std::vector<CapabilitySource> &sources() const { return sources_; }

    /// Total number of stored facts across every domain.
    size_t fact_count() const;

    // -- serialisation -----------------------------------------------------

    /**
     * @brief Render the model as canonical JSON.
     *
     * Shape:
     * @code
     * { "schema_version": 4, "generated_at_ms": 0,
     *   "capabilities": { "vulkan": { ... } },
     *   "provenance":   { "vulkan": { "_source": "fluxd.vulkan" } },
     *   "sources":      [ { "id": "fluxd.vulkan", ... } ] }
     * @endcode
     */
    std::string to_json(bool pretty = true) const;

    /**
     * @brief Parse canonical JSON produced by to_json().
     *
     * A newer schema_version parses successfully: unknown domains and keys are kept
     * verbatim so a v4 reader never destroys v5 facts it is merely passing through.
     * @return false with @p error set when the text is not valid model JSON.
     */
    static bool from_json(const std::string &text, CapabilityModel &out, std::string &error);

    /// Write to_json() to @p path via a temp file + rename, so readers never see a partial file.
    bool write_file(const std::string &path, bool pretty = true) const;

    /// Read and parse @p path. Returns false when it cannot be opened or parsed.
    static bool read_file(const std::string &path, CapabilityModel &out, std::string &error);

private:
    std::map<std::string, CapabilityDomain> domains_;
    std::vector<CapabilitySource> sources_;
};

} // namespace flux::gfx
