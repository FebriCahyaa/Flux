#include "CompatTypes.hpp"

namespace flux::compat {

const char *to_string(Confidence v) {
    switch (v) {
    case Confidence::High: return "high";
    case Confidence::Medium: return "medium";
    case Confidence::Low: return "low";
    case Confidence::Unknown: return "unknown";
    }
    return "unknown";
}

const char *to_string(Layer v) {
    switch (v) {
    case Layer::Device: return "device";
    case Layer::Cpu: return "cpu";
    case Layer::Gpu: return "gpu";
    case Layer::Display: return "display";
    }
    return "device";
}

const char *to_string(Mode v) {
    switch (v) {
    case Mode::Real: return "real";
    case Mode::Auto: return "auto";
    case Mode::Compatibility: return "compatibility";
    case Mode::Advanced: return "advanced";
    case Mode::Custom: return "custom";
    }
    return "real";
}

const char *to_string(GateKind v) {
    switch (v) {
    case GateKind::None: return "none";
    case GateKind::Unknown: return "unknown";
    case GateKind::DeviceIdentity: return "device_identity";
    case GateKind::CpuIdentity: return "cpu_identity";
    case GateKind::GpuIdentity: return "gpu_identity";
    case GateKind::DisplayRefresh: return "display_refresh";
    case GateKind::ServerControlled: return "server_controlled";
    case GateKind::Entitlement: return "entitlement";
    case GateKind::HardwareInsufficient: return "hardware_insufficient";
    }
    return "unknown";
}

const char *to_string(Tri v) {
    switch (v) {
    case Tri::Yes: return "yes";
    case Tri::No: return "no";
    case Tri::Unknown: return "unknown";
    }
    return "unknown";
}

const char *to_string(LayerState v) {
    switch (v) {
    case LayerState::Real: return "real";
    case LayerState::Auto: return "auto";
    case LayerState::Applied: return "applied";
    case LayerState::Available: return "available";
    case LayerState::Unsupported: return "unsupported";
    case LayerState::Unknown: return "unknown";
    case LayerState::Partial: return "partial";
    case LayerState::Verified: return "verified";
    case LayerState::Restored: return "restored";
    case LayerState::Failed: return "failed";
    }
    return "unknown";
}

const char *to_string(BackendState v) {
    switch (v) {
    case BackendState::Unavailable: return "unavailable";
    case BackendState::NotConfigured: return "not_configured";
    case BackendState::Unsupported: return "unsupported";
    case BackendState::Available: return "available";
    }
    return "unavailable";
}

const char *to_string(ContextState v) {
    switch (v) {
    case ContextState::Inactive: return "inactive";
    case ContextState::Preparing: return "preparing";
    case ContextState::Active: return "active";
    case ContextState::Restoring: return "restoring";
    case ContextState::Restored: return "restored";
    case ContextState::Failed: return "failed";
    }
    return "inactive";
}

std::optional<Mode> parse_mode(std::string_view s) {
    for (Mode m : {Mode::Real, Mode::Auto, Mode::Compatibility, Mode::Advanced, Mode::Custom})
        if (s == to_string(m)) return m;
    return std::nullopt;
}

std::optional<GateKind> parse_gate(std::string_view s) {
    for (GateKind g : {GateKind::None, GateKind::Unknown, GateKind::DeviceIdentity, GateKind::CpuIdentity,
                       GateKind::GpuIdentity, GateKind::DisplayRefresh, GateKind::ServerControlled,
                       GateKind::Entitlement, GateKind::HardwareInsufficient})
        if (s == to_string(g)) return g;
    return std::nullopt;
}

} // namespace flux::compat
