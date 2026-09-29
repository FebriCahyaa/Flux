#include "Arming.hpp"

namespace flux::compat {

namespace {
bool write_state(const Io &io, const std::string &path, const std::string &content) {
    if (io.write_atomic) return io.write_atomic(path, content);
    return io.write && io.write(path, content);
}
} // namespace

const char *to_string(ProviderState s) {
    switch (s) {
    case ProviderState::Unavailable: return "unavailable";
    case ProviderState::NotConfigured: return "not_configured";
    case ProviderState::Unsupported: return "unsupported";
    case ProviderState::Installed: return "installed";
    case ProviderState::Loaded: return "loaded";
    }
    return "unavailable";
}

bool Arming::installed() const {
    if (!io_.exists) return false;
    for (const auto &f : cfg_.disabled_flags)
        if (io_.exists(f)) return false;
    for (const auto &l : cfg_.provider_libs)
        if (io_.exists(l)) return true;
    return false;
}

bool Arming::loaded() const {
    if (!io_.exists || !io_.read || !io_.exists(cfg_.info_file)) return false;
    auto text = io_.read(cfg_.info_file);
    provider::ProviderInfo pi;
    std::string err;
    if (!text || !provider::provider_info_from_json(*text, pi, err)) return false;
    return pi.loaded && env_.boot_id && !pi.boot_id.empty() && pi.boot_id == env_.boot_id();
}

BackendState Arming::backend_state() const {
    if (sdk_ < cfg_.min_sdk) return BackendState::Unsupported;
    if (!installed()) return BackendState::Unavailable;
    if (!cfg_.user_enabled) return BackendState::NotConfigured;
    return BackendState::Available;
}

ProviderState Arming::provider_state() const {
    if (sdk_ < cfg_.min_sdk) return ProviderState::Unsupported;
    if (!installed()) return ProviderState::Unavailable;
    if (!cfg_.user_enabled) return ProviderState::NotConfigured;
    return loaded() ? ProviderState::Loaded : ProviderState::Installed;
}

std::string Arming::arm(const Resolution &res, const ProfileLibrary &lib, const provider::ProcessScope &scope,
                        const std::string &profile_name, std::string &error) {
    if (backend_state() != BackendState::Available) {
        error = std::string("provider ") + to_string(provider_state());
        return {};
    }
    const std::string path = cfg_.plans_dir + "/" + res.package + ".json";
    provider::Plan plan = provider::make_plan(res, lib, env_.boot_id ? env_.boot_id() : "", env_.daemon_pid ? env_.daemon_pid() : 0,
                                              env_.now_ms ? env_.now_ms() : 0, cfg_.lease_ms, scope, profile_name);
    if (!plan.active) { // nothing to override for this package: make sure nothing stays armed
        disarm(res.package);
        return {};
    }
    // Same package + same plan => same id: re-arming (renewing the lease) does not make the
    // running process look stale.
    if (!write_state(io_, path, provider::plan_to_json(plan))) {
        error = "could not write the plan";
        return {};
    }
    provider::ArmedEntry e;
    e.package = res.package;
    e.app_id = env_.app_id_of ? env_.app_id_of(res.package) : 0;
    e.transaction_id = plan.transaction_id;
    armed_[res.package] = e;
    if (!write_armed_list()) {
        error = "could not write the armed list";
        armed_.erase(res.package);
        return {};
    }
    return plan.transaction_id;
}

void Arming::disarm(const std::string &package) {
    auto it = armed_.find(package);
    if (it == armed_.end()) return;
    armed_.erase(it);
    write_armed_list();
    // The plan file is made inactive rather than deleted (Io has no unlink); the provider only
    // consults packages named in armed.list and rejects an inactive plan anyway.
    provider::Plan dead;
    dead.version = provider::kPlanVersion;
    dead.package = package;
    dead.active = false;
    write_state(io_, cfg_.plans_dir + "/" + package + ".json", provider::plan_to_json(dead));
}

void Arming::disarm_all() {
    std::vector<std::string> pkgs;
    for (const auto &[p, e] : armed_) pkgs.push_back(p);
    for (const auto &p : pkgs) disarm(p);
    write_state(io_, cfg_.armed_list_file, "");
}

std::string Arming::transaction_of(const std::string &package) const {
    auto it = armed_.find(package);
    return it == armed_.end() ? std::string() : it->second.transaction_id;
}

bool Arming::write_armed_list() {
    std::vector<provider::ArmedEntry> v;
    for (const auto &[p, e] : armed_) v.push_back(e);
    return write_state(io_, cfg_.armed_list_file, provider::armed_list_to_text(v));
}

std::optional<provider::ProcStatus> Arming::process_status(int64_t pid) const {
    if (pid <= 1 || !io_.exists || !io_.read) return std::nullopt;
    if (env_.pid_alive && !env_.pid_alive(pid)) return std::nullopt;
    const std::string path = cfg_.proc_dir + "/" + std::to_string(pid) + ".json";
    if (!io_.exists(path)) return std::nullopt;
    auto text = io_.read(path);
    provider::ProcStatus s;
    std::string err;
    if (!text || !provider::proc_status_from_json(*text, s, err)) return std::nullopt;
    if (s.pid != pid) return std::nullopt; // file does not describe this process
    return s;
}

} // namespace flux::compat
