#pragma once

// Contract between the Flux daemon and the Zygisk compatibility provider.
//
// The daemon *arms* a plan per package before the game is launched (a process gets its
// identity when it is created, long before Flux sees it in the foreground). The provider,
// running at process specialization, decides from the plan alone whether this process is a
// target, and applies exactly the layers the plan lists. The plan is the resolver's output;
// the provider never computes an override of its own.
//
// Everything here is pure logic (no JNI, no device access) so it is host-tested.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "CompatTypes.hpp"
#include "GameProfile.hpp"
#include "Resolver.hpp"

namespace flux::compat::provider {

inline constexpr int kPlanVersion = 2;

/// Which processes of the package receive the plan. Never guessed from a name substring.
struct ProcessScope {
    enum class Kind { Main, Listed, All } kind = Kind::Main; ///< Main: only the process named exactly like the package
    std::vector<std::string> processes;                      ///< Listed: exact process names ("com.x:remote")
};

/// Fields the provider can honour, per layer. Anything else is rejected, not dropped silently.
const std::vector<std::string> &supported_fields(Layer layer);
/// Which layer owns an identity field name (device / cpu / gpu), if any.
std::optional<Layer> layer_of_field(const std::string &field);

struct Plan {
    int version = 0;
    bool active = false;
    std::string package;
    std::string transaction_id;
    std::string boot_id;
    int64_t daemon_pid = 0;
    int64_t armed_at_ms = 0;
    int64_t expires_at_ms = 0; ///< 0 = no time limit (boot id + daemon liveness still apply)
    std::string profile;
    std::string mode;
    ProcessScope scope;
    std::vector<Layer> layers;                                   ///< exactly the resolver's required identity layers
    std::map<std::string, std::map<std::string, std::string>> identities; ///< layer name -> field -> value
};

/// Build the plan for a resolution. Only identity layers (device/cpu/gpu) are included: the
/// display layer is the refresh writer's business, not the provider's.
Plan make_plan(const Resolution &res, const ProfileLibrary &lib, const std::string &boot_id, int64_t daemon_pid,
               int64_t now_ms, int64_t lease_ms, const ProcessScope &scope, const std::string &profile_name);

std::string plan_to_json(const Plan &p);
bool plan_from_json(const std::string &json, Plan &out, std::string &error);

/// Stable id: same package + layers + values + scope + boot => same id. A changed plan gets a new one,
/// so a process that applied the old plan is recognisably stale.
std::string compute_transaction_id(const Plan &p);

// -- validation ------------------------------------------------------------------------------

enum class Reject {
    None,
    InvalidJson,
    WrongVersion,
    Inactive,
    NoTransaction,
    TransactionMismatch,
    BootMismatch,
    DaemonGone,
    Expired,
    PackageMismatch,
    ProcessNotInScope,
    NoLayers,
    UnsupportedField,
    InvalidValue,
    CapabilityClaim
};
const char *to_string(Reject r);

struct Env {
    int64_t now_ms = 0;
    std::string boot_id;
    bool daemon_alive = false;
};

struct LayerVerdict {
    Layer layer;
    bool usable = false;
    std::string reason; ///< why not, precise
};

struct Validation {
    Reject reject = Reject::None; ///< plan-level rejection: apply nothing
    std::string detail;
    std::vector<LayerVerdict> layers; ///< per-layer: a bad layer is dropped, the rest may apply
    bool ok() const { return reject == Reject::None; }
};

/// Plan-level checks (version, active, transaction, boot, daemon, expiry) and per-layer field checks.
Validation validate_plan(const Plan &p, const Env &env);

// -- process matching --------------------------------------------------------------------------

struct ProcessInfo {
    std::string nice_name; ///< process name as given at specialization ("com.x", "com.x:remote")
    std::string package;   ///< package that owns the UID, resolved by the privileged side; empty if unknown
    int uid = 0;
};

struct Match {
    bool matched = false;
    Reject reject = Reject::None;
    std::string detail;
};

/// Does this process receive the plan? The package must be the UID's owner (not merely a name
/// prefix) and the process must be in the plan's scope.
Match match_process(const Plan &p, const ProcessInfo &proc);

// -- what to apply -----------------------------------------------------------------------------

/// A Java Build field, a native property, a GL/EGL string or a Vulkan property.
struct Items {
    std::map<std::string, std::string> build;   ///< android.os.Build static field -> value
    std::map<std::string, std::string> props;   ///< system property key -> value (native property reads)
    std::map<std::string, std::string> gl;      ///< vendor / renderer / version / egl_vendor
    struct Vulkan {
        std::string device_name;
        std::optional<uint32_t> vendor_id, device_id, api_version, driver_version;
        bool any() const { return !device_name.empty() || vendor_id || device_id || api_version || driver_version; }
    } vk;
    bool empty() const { return build.empty() && props.empty() && gl.empty() && !vk.any(); }
};

/// Turn validated layers into concrete items. Layers listed as unusable contribute nothing.
Items items_from_plan(const Plan &p, const Validation &v);

/// Encode "1.3.0" as VK_MAKE_API_VERSION(0,1,3,0); nullopt for anything that is not major.minor[.patch].
std::optional<uint32_t> parse_vk_api_version(const std::string &s);
/// A Vulkan API version may be reported lower than the real one, never higher.
inline bool vk_api_version_allowed(uint32_t wanted, uint32_t real) { return wanted <= real; }

// -- provider <-> companion wire format -----------------------------------------------------------

/// The reply the privileged companion gives a process that asked whether it is a target.
struct Decision {
    enum class Result { NotTarget, Target, Rejected } result = Result::NotTarget;
    Reject reject = Reject::None;
    std::string detail;
    std::string package, transaction_id, profile;
    Items items;
    std::vector<LayerVerdict> layers;
};
std::string decision_to_json(const Decision &d);
bool decision_from_json(const std::string &json, Decision &out, std::string &error);

// -- per-process status (companion writes, daemon reads) --------------------------------------------

enum class ProcState { Matched, Applied, Verified, Failed, Ended };
const char *to_string(ProcState s);

struct LayerResult {
    Layer layer;
    std::string state;  ///< applied | verified | installed | observed | unsupported | failed
    std::string detail;
};

struct ProcStatus {
    std::string package, process, transaction_id;
    int64_t pid = 0;
    int uid = 0;
    ProcState state = ProcState::Matched;
    std::vector<LayerResult> layers;
    int64_t updated_ms = 0;
};
std::string proc_status_to_json(const ProcStatus &s);
bool proc_status_from_json(const std::string &json, ProcStatus &out, std::string &error);

/// Provider-level readiness (written when the companion starts).
struct ProviderInfo {
    bool loaded = false;
    int api_version = 0;
    std::string boot_id;
    int64_t companion_pid = 0;
    int64_t started_ms = 0;
};
std::string provider_info_to_json(const ProviderInfo &p);
bool provider_info_from_json(const std::string &json, ProviderInfo &out, std::string &error);

/// armed.list: what the in-process filter reads with one tiny read. One "<package>\t<app_id>\t<tx>" per line.
struct ArmedEntry {
    std::string package;
    int app_id = 0; ///< uid % 100000, so every Android user matches
    std::string transaction_id;
};
std::string armed_list_to_text(const std::vector<ArmedEntry> &entries);
std::vector<ArmedEntry> armed_list_from_text(const std::string &text);
/// Cheap pre-filter for the app process, before any IPC: could this process be a target?
bool armed_candidate(const std::vector<ArmedEntry> &entries, const std::string &nice_name, int uid);

} // namespace flux::compat::provider
