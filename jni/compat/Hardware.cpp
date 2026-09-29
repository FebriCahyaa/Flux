#include "Hardware.hpp"

#include <algorithm>

#include "CapabilityModel.hpp"

namespace flux::compat {

double RealHardware::peak_hz() const {
    double best = refresh_peak_hz;
    for (double m : refresh_modes_hz) best = std::max(best, m);
    return best;
}

Tri RealHardware::panel_supports(double hz) const {
    if (refresh_modes_hz.empty() && refresh_peak_hz <= 0) return Tri::Unknown;
    // Modes are floats on the wire (119.99 for a "120 Hz" panel): tolerate 1 Hz.
    return peak_hz() + 1.0 >= hz ? Tri::Yes : Tri::No;
}

RealHardware hardware_from_model(const flux::gfx::CapabilityModel &m) {
    namespace s = flux::gfx::schema;
    RealHardware hw;
    auto str = [&](const char *d, const char *k) { return m.get_string(d, k).value_or(""); };

    hw.soc = str(s::domain::kRuntime, s::runtime::kSocModel);
    hw.abi = str(s::domain::kRuntime, s::runtime::kAbi);
    hw.sdk = m.get_int(s::domain::kRuntime, s::runtime::kAndroidSdk).value_or(0);
    if (auto gki = m.get_bool(s::domain::kRuntime, s::runtime::kKernelIsGki))
        hw.kernel_gki = *gki ? Tri::Yes : Tri::No;

    hw.gpu_vendor = str(s::domain::kGpu, s::gpu::kVendor);
    hw.gpu_model = str(s::domain::kGpu, s::gpu::kModel);

    if (auto v = m.get_bool(s::domain::kVulkan, s::vulkan::kAvailable))
        hw.vulkan = *v ? Tri::Yes : Tri::No;
    hw.vulkan_device = str(s::domain::kVulkan, s::vulkan::kDeviceName);
    hw.vulkan_api = str(s::domain::kVulkan, s::vulkan::kApiVersion);

    hw.refresh_current_hz = m.get_double(s::domain::kDisplay, s::display::kRefreshRateHz).value_or(0);
    hw.refresh_peak_hz = m.get_double(s::domain::kDisplay, s::display::kPeakRefreshRateHz).value_or(0);
    hw.refresh_min_hz = m.get_double(s::domain::kDisplay, s::display::kMinRefreshRateHz).value_or(0);
    // supported_modes is a list of "WxH@Hz" strings or bare rates; keep the numeric tail.
    if (auto modes = m.get_list(s::domain::kDisplay, s::display::kSupportedModes)) {
        for (const std::string &mode : *modes) {
            size_t at = mode.rfind('@');
            std::string tail = at == std::string::npos ? mode : mode.substr(at + 1);
            try {
                size_t used = 0;
                double hz = std::stod(tail, &used);
                if (used > 0 && hz > 0) hw.refresh_modes_hz.push_back(hz);
            } catch (...) {
            }
        }
    }
    return hw;
}

} // namespace flux::compat
