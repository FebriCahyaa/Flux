// Configuration namespace compatibility (Phase 4.5).
//
// The runtime keeps reading /data/adb/.config/flux (frozen). This helper defines the documented
// migration path towards /data/adb/.config/zairenkai without any destructive step:
//   resolve()  compatibility-aware lookup: the Zairenkai copy when it exists, else the legacy file.
//   plan()     what a migration would do: copy legacy -> new only when the new file is absent.
//   apply()    executes a plan through create-only writes. Never deletes, moves, truncates or
//              overwrites; the legacy file always stays. Idempotent: a second run copies nothing.
// Not invoked by fluxd in this phase (no behavior change); see BRAND_MIGRATION.md.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace zairenkai::brand {

struct ConfigFs {
    std::function<bool(const std::string &path)> exists;
    std::function<std::optional<std::string>(const std::string &path)> read;
    /// Must fail (false) when `path` already exists: no overwrite is possible through this seam.
    std::function<bool(const std::string &path, const std::string &content)> create_new;
};

struct ConfigRoots {
    std::string legacy = "/data/adb/.config/flux";
    std::string current = "/data/adb/.config/zairenkai/config";
};

/// Path to read `name` from: current/name if it exists, else legacy/name (even if missing).
std::string resolve(const ConfigFs &fs, const ConfigRoots &roots, const std::string &name);

enum class StepKind { Copy, KeepExisting, NoLegacy, Rejected };
const char *to_string(StepKind k);

struct MigrationStep {
    std::string name, from, to;
    StepKind kind = StepKind::NoLegacy;
};

/// Names must be plain file names (no '/', no "..", not empty); others are Rejected.
std::vector<MigrationStep> plan(const ConfigFs &fs, const ConfigRoots &roots, const std::vector<std::string> &names);

struct MigrationReport {
    int copied = 0, kept = 0, missing = 0, rejected = 0, failed = 0;
    std::vector<std::string> failures;
};

MigrationReport apply(const ConfigFs &fs, const std::vector<MigrationStep> &steps);

} // namespace zairenkai::brand
