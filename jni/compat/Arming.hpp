#pragma once

// Arming: the daemon side of the provider contract.
//
// A process gets its identity when it is created, so the plan has to exist BEFORE the game is
// launched. The daemon therefore arms a plan per profiled package while it runs (at start, and
// whenever a session begins or ends) and disarms all of them when it stops. An armed plan is
// configuration, not applied state: nothing changes until a matching process is created, and the
// process-local context disappears with that process. A plan left behind by a crashed daemon is
// inert on its own (boot id and daemon liveness are checked by the provider).

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ProviderPlan.hpp"
#include "Runtime.hpp"

namespace flux::compat {

struct ArmingConfig {
    std::string plans_dir = "/data/adb/.config/flux/compat_provider/plans";
    std::string proc_dir = "/data/adb/.config/flux/compat_provider/proc";
    std::string info_file = "/data/adb/.config/flux/compat_provider/provider.json";
    std::string armed_list_file = "/data/adb/modules/flux/armed.list";
    /// Installed provider libraries (zygisk/<abi>.so). Any one present counts as installed.
    std::vector<std::string> provider_libs = {"/data/adb/modules/flux/zygisk/arm64-v8a.so",
                                              "/data/adb/modules/flux/zygisk/armeabi-v7a.so"};
    /// If any of these exists the module is disabled/being removed and the provider will not load.
    std::vector<std::string> disabled_flags = {"/data/adb/modules/flux/disable", "/data/adb/modules/flux/remove"};
    int64_t lease_ms = 24LL * 3600 * 1000;
    bool user_enabled = false; ///< explicit opt-in; the provider is never armed without it
    int64_t min_sdk = 26;
};

struct ArmingEnv {
    std::function<int64_t()> now_ms;
    std::function<std::string()> boot_id;
    std::function<int64_t()> daemon_pid;
    std::function<bool(int64_t)> pid_alive;
    std::function<int(const std::string &package)> app_id_of; ///< uid % 100000; 0 if unknown
};

/// What the UI shows for the provider as a whole.
enum class ProviderState { Unavailable, NotConfigured, Unsupported, Installed, Loaded };
const char *to_string(ProviderState s);

class Arming {
public:
    Arming(Io io, ArmingConfig cfg, ArmingEnv env, int64_t sdk) : io_(std::move(io)), cfg_(std::move(cfg)), env_(std::move(env)), sdk_(sdk) {}

    /// The user's opt-in can change while the daemon runs (WebUI toggle).
    void set_user_enabled(bool v) { cfg_.user_enabled = v; }
    /// Installed + opted in + new enough. Says nothing about whether Zygisk itself is enabled:
    /// that is only known once the provider has actually loaded (loaded()).
    BackendState backend_state() const;
    ProviderState provider_state() const;
    bool installed() const;
    /// The provider's companion ran in this boot, i.e. Zygisk really loaded the module.
    bool loaded() const;

    /// Arm (or disarm, when the resolution needs no identity layer) the plan for one package.
    /// Returns the transaction id, empty when nothing is armed.
    std::string arm(const Resolution &res, const ProfileLibrary &lib, const provider::ProcessScope &scope,
                    const std::string &profile_name, std::string &error);
    void disarm(const std::string &package);
    void disarm_all();
    std::string transaction_of(const std::string &package) const;

    /// Status the provider recorded for @p pid, only while that process is still alive.
    std::optional<provider::ProcStatus> process_status(int64_t pid) const;
    /// Drop stale per-process status files' relevance at boot: the daemon ignores any whose
    /// transaction it did not arm in this run, so nothing needs deleting from disk.
    const ArmingConfig &config() const { return cfg_; }

private:
    bool write_armed_list();
    Io io_;
    ArmingConfig cfg_;
    ArmingEnv env_;
    int64_t sdk_;
    std::map<std::string, provider::ArmedEntry> armed_;
};

} // namespace flux::compat
