#pragma once

// Vocabulary shared by the Flux compatibility engine (FCE).
//
// Everything in jni/compat is pure logic behind injectable seams: no function in
// this directory reaches /sys, /proc or /data on its own, so all of it is
// exercised by the host test harness.

#include <optional>
#include <string>
#include <string_view>

namespace flux::compat {

enum class Confidence { High, Medium, Low, Unknown };

/// The four independent things a game can gate on. A plan only ever contains the
/// layers the game actually checks.
enum class Layer { Device, Cpu, Gpu, Display };

enum class Mode { Real, Auto, Compatibility, Advanced, Custom };

enum class GateKind {
    None,                ///< analysed; nothing gates the target
    Unknown,             ///< could not be determined
    DeviceIdentity,
    CpuIdentity,
    GpuIdentity,
    DisplayRefresh,
    ServerControlled,    ///< decided server side; nothing local can satisfy it
    Entitlement,         ///< account / purchase gate
    HardwareInsufficient ///< the hardware really cannot do it
};

/// Three-valued answer for facts we may not be able to establish.
enum class Tri { Yes, No, Unknown };

/// Per-layer state shown in the WebUI. Deliberately not a boolean "unlocked".
enum class LayerState { Real, Auto, Applied, Available, Unsupported, Unknown, Partial, Verified, Restored, Failed };

enum class BackendState { Unavailable, NotConfigured, Unsupported, Available };
enum class ContextState { Inactive, Preparing, Active, Restoring, Restored, Failed };

const char *to_string(Confidence v);
const char *to_string(Layer v);
const char *to_string(Mode v);
const char *to_string(GateKind v);
const char *to_string(Tri v);
const char *to_string(LayerState v);
const char *to_string(BackendState v);
const char *to_string(ContextState v);

std::optional<Mode> parse_mode(std::string_view s);
std::optional<GateKind> parse_gate(std::string_view s);

} // namespace flux::compat
