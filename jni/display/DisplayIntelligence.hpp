// Zairenkai Display & Rendering capability foundation (Step 8.5) — read-only.
//
// What the panel and the compositor *can* do: refresh modes, current mode refresh, min/max,
// adaptive refresh, resolution, HDR; SurfaceFlinger, composer HAL, RenderEngine backend, frame
// timing availability, rendering pipeline. Published to CapabilityContext domains "display" and
// "rendering".
//
// Refresh capability is not FPS: `display.refresh.*` describe panel modes. Frames actually
// presented are measured elsewhere (SessionRecorder) and never published here.
// No policy, no refresh forcing, no frame boosting, no injection, no SurfaceFlinger changes.
#pragma once

#include "CapabilityContext.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace flux::display {

inline constexpr const char *kDisplayDomain = "display";
inline constexpr const char *kRenderingDomain = "rendering";

/// Raw evidence. Text sources are nullopt when not collected (-> Unknown), "" when collected but empty.
struct DisplayEvidence {
    std::optional<std::string> dumpsys_display; // `dumpsys display`
    std::optional<std::string> wm_size;         // `wm size`
    std::optional<std::string> service_list;    // `service list`
    std::function<std::string(const std::string &key)> property;
    const flux::context::CapabilityContext *context = nullptr; // kernel drm modes, graphics facts
};

struct DisplayMode {
    int id = 0, width = 0, height = 0;
    double refresh_hz = 0;
    std::vector<double> alternative_hz;
};

struct ParsedDisplay {
    std::vector<DisplayMode> modes;
    int active_mode_id = -1;
    std::optional<std::vector<int>> hdr_types; // nullopt = no HdrCapabilities in the text
};

/// Parses the first built-in DisplayDeviceInfo of `dumpsys display`.
ParsedDisplay parse_dumpsys_display(const std::string &text);

std::vector<flux::context::CapabilityFact> observe_display(const DisplayEvidence &e);
std::vector<flux::context::CapabilityFact> observe_rendering(const DisplayEvidence &e);

/// Publishes both domains (publishers "display" and "rendering"; snapshot replace).
void publish(const DisplayEvidence &e, flux::context::CapabilityContext &context);

} // namespace flux::display
