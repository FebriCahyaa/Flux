#pragma once

// GameRuntime: the one entry point the daemon calls when a game becomes the
// foreground app and again when it goes away.
//
//   activate():  resolve -> pick backend -> compat tx -> perf tx -> report
//   deactivate(): restore both transactions in reverse, verify, report
//
// The performance transaction is independent of the compatibility one: a backend
// failure costs the game its compatibility context, never its performance profile.

#include <memory>

#include "PerfPlanner.hpp"
#include "Runtime.hpp"

namespace flux::compat {

struct Activation {
    Resolution resolution;
    std::string backend = "none";
    BackendState backend_state = BackendState::Unavailable;
    ContextState context = ContextState::Inactive;   ///< compatibility context
    ContextState perf_context = ContextState::Inactive;
    std::vector<CategoryStatus> perf;
    std::vector<std::string> skipped, log;
    double refresh_request_hz = 0;   ///< handed to the existing RefreshHold path; 0 = leave alone
    std::string refresh_note;
    /// Real hardware next to the identity the game will see, for the two-view UI.
    std::map<std::string, std::string> effective_identity;
    /// Process-level context as the provider reports it (empty state when there is no provider).
    ProviderReport provider;
    std::string to_json() const;
};

struct RuntimeDeps {
    Io io;
    NativeBackend *native = nullptr;
    Backend *zygisk = nullptr; ///< optional; null when not configured
    const ProfileLibrary *library = nullptr;
    std::vector<std::string> block_queues;
    std::function<bool(const std::string &)> mitigation_allows;
};

class GameRuntime {
public:
    explicit GameRuntime(RuntimeDeps deps) : d_(std::move(deps)) {}

    /// Compatibility first: resolve, pick the backend, apply and verify the compatibility
    /// context, compute the refresh request. Touches no memory/touch/storage node.
    Activation activate_compat(const EffectiveProfile &profile, const std::optional<GameRequirement> &known,
                               const RealHardware &hw, int pid = 0, int uid = 0);
    /// Per-game memory/touch/storage overrides. Called after Flux has applied its own
    /// performance profile, so the game's explicit choice wins over the profile script.
    void activate_perf(Activation &a);
    /// Write the overrides again after the profile script ran again (e.g. a thermal
    /// performance <-> performance_lite switch). Keeps the original snapshots.
    bool reassert_perf();
    /// activate_compat() followed by activate_perf(): the one-call form used by tests and tools.
    Activation activate(const EffectiveProfile &profile, const std::optional<GameRequirement> &known,
                        const RealHardware &hw);
    /// Restore everything. Returns true when both transactions restored cleanly.
    bool deactivate();

    bool active() const { return compat_ || perf_; }
    const std::string &package() const { return package_; }
    /// Journal of everything still applied, for crash recovery.
    std::vector<std::string> journal() const;

private:
    RuntimeDeps d_;
    std::string package_;
    std::unique_ptr<Transaction> compat_, perf_;
    std::unique_ptr<Resolution> plan_; // BackendAction borrows it
    EffectiveProfile profile_;         // remembered for activate_perf()
    Backend *backend_ = nullptr;
};

} // namespace flux::compat
