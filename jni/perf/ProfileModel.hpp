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

// Performance profile inheritance (docs/architecture/PERFORMANCE_PROFILE_MODEL.md).
//
//   BUILTIN defaults -> GLOBAL -> PRESET chain ("extends", root first) -> GAME -> RUNTIME
//
// Performance fields only. Legacy documents that also carry compatibility/identity data are
// still readable; those parts are reported as ignored and never resolved.

#include "PerformancePlanner.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace flux::perf {

/// One inheritance layer: every field optional; a later layer replaces only what it sets.
struct ProfileLayer {
    std::string extends; ///< preset name; empty = none
    std::optional<std::string> profile; ///< performance | performance_lite | balance | powersave
    std::optional<std::string> memory, touch, storage, refresh;
    std::optional<int> refresh_custom_hz;
    std::optional<bool> launch_boost;
};

struct ProfileDocument {
    ProfileLayer global;
    std::map<std::string, ProfileLayer> presets;
    std::map<std::string, ProfileLayer> games; ///< keyed by package
};

enum class Format { Current, LegacyGameProfiles, LegacyLibrary };
const char *to_string(Format f);

struct LoadResult {
    bool ok = false;
    Format format = Format::Current;
    std::vector<std::string> errors;   ///< the document (or entry) is rejected
    std::vector<std::string> warnings; ///< ignored legacy content, e.g. compatibility/identities
};

/// Parse a profile document. Accepts the current format and both legacy formats. Invalid entries
/// are rejected with an error naming the entry; other entries still load.
LoadResult load_document(const std::string &json, ProfileDocument &out);

/// Merge another document's presets/games (e.g. a legacy library of presets) without overwriting.
void merge_document(ProfileDocument &into, const ProfileDocument &from);

/// Legacy gamelist.json entry (main): lite_mode=true pins the game to performance_lite.
ProfileLayer layer_from_gamelist(bool lite_mode);

/// Where a resolved field came from.
struct FieldSource {
    std::string layer;  ///< builtin | global | preset | game | runtime
    std::string name;   ///< preset name or package; empty otherwise

    std::string describe() const;  ///< "preset gaming", "game override", "global", ...
};

struct ResolvedProfile {
    std::string profile = "performance";
    PerfProfile perf;                            ///< planner input
    std::map<std::string, FieldSource> sources;  ///< field -> source
    std::vector<std::string> chain;              ///< layers applied, lowest first
    std::vector<std::string> errors;             ///< unknown profile, missing parent, cycle
    bool ok() const { return errors.empty(); }

    /// "memory:\n  source=preset gaming\n..." for diagnostics.
    std::string explain() const;
};

/// Resolve the effective performance profile for @p package. An unknown preset, a missing parent or
/// a cycle stops the chain at that point and is reported; lower layers still apply.
ResolvedProfile resolve(const ProfileDocument &doc, const std::string &package,
                        const std::optional<ProfileLayer> &runtime = std::nullopt);

} // namespace flux::perf
