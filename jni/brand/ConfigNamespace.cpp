#include "ConfigNamespace.hpp"

namespace zairenkai::brand {

namespace {
bool plain_name(const std::string &n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos && n.find('\0') == std::string::npos;
}
} // namespace

std::string resolve(const ConfigFs &fs, const ConfigRoots &roots, const std::string &name) {
    const std::string current = roots.current + "/" + name;
    if (plain_name(name) && fs.exists && fs.exists(current)) return current;
    return roots.legacy + "/" + name;
}

const char *to_string(StepKind k) {
    switch (k) {
    case StepKind::Copy: return "copy";
    case StepKind::KeepExisting: return "keep_existing";
    case StepKind::NoLegacy: return "no_legacy";
    case StepKind::Rejected: return "rejected";
    }
    return "unknown";
}

std::vector<MigrationStep> plan(const ConfigFs &fs, const ConfigRoots &roots, const std::vector<std::string> &names) {
    std::vector<MigrationStep> out;
    for (const auto &n : names) {
        MigrationStep s;
        s.name = n;
        if (!plain_name(n)) {
            s.kind = StepKind::Rejected;
            out.push_back(s);
            continue;
        }
        s.from = roots.legacy + "/" + n;
        s.to = roots.current + "/" + n;
        if (fs.exists(s.to)) s.kind = StepKind::KeepExisting;
        else if (!fs.exists(s.from)) s.kind = StepKind::NoLegacy;
        else s.kind = StepKind::Copy;
        out.push_back(s);
    }
    return out;
}

MigrationReport apply(const ConfigFs &fs, const std::vector<MigrationStep> &steps) {
    MigrationReport r;
    for (const auto &s : steps) {
        switch (s.kind) {
        case StepKind::KeepExisting: ++r.kept; continue;
        case StepKind::NoLegacy: ++r.missing; continue;
        case StepKind::Rejected: ++r.rejected; continue;
        case StepKind::Copy: break;
        }
        if (fs.exists(s.to)) { // appeared since planning: never overwrite
            ++r.kept;
            continue;
        }
        const auto content = fs.read(s.from);
        if (!content || !fs.create_new(s.to, *content)) {
            ++r.failed;
            r.failures.push_back(s.name);
            continue;
        }
        ++r.copied;
    }
    return r;
}

} // namespace zairenkai::brand
