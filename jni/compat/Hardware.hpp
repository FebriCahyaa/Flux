#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "CompatTypes.hpp"

namespace flux::gfx {
class CapabilityModel;
}

namespace flux::compat {

/// What the machine actually is. Nothing here is ever fed from a compatibility
/// profile: a spoofed identity is not evidence of a capability, so the resolver
/// reads this struct and only this struct when it asks "can the hardware do it".
struct RealHardware {
    std::string brand, model;
    std::string soc, abi;
    std::string gpu_vendor, gpu_model;
    Tri vulkan = Tri::Unknown;
    std::string vulkan_device, vulkan_api;
    double refresh_current_hz = 0, refresh_peak_hz = 0, refresh_min_hz = 0;
    std::vector<double> refresh_modes_hz;
    int64_t ram_mb = 0;
    int64_t sdk = 0;
    Tri kernel_gki = Tri::Unknown;

    /// True when the panel exposes a mode of at least @p hz. Unknown when the
    /// panel's modes were never read.
    Tri panel_supports(double hz) const;
    /// Peak mode, from the mode list when present, else the reported peak.
    double peak_hz() const;
};

/// Fold the shared capability model (SynthesisCore + fluxd probes) into RealHardware.
/// Brand/model/RAM are not part of that schema; callers add them.
RealHardware hardware_from_model(const flux::gfx::CapabilityModel &model);

} // namespace flux::compat
