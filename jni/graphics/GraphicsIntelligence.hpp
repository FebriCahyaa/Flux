// Zairenkai Graphics Intelligence foundation (Step 8) — capability only, read-only.
//
// Answers what GPU and graphics stack the device has: vendor, model, driver, Vulkan, OpenGL ES /
// EGL, and whether GPU frequency / load interfaces exist. Facts are published into the shared
// CapabilityContext under domain "graphics", publisher "graphics".
//
// Capability is separate from policy: nothing here decides, tunes, overclocks, injects, or
// upscales. A future Graphics Policy reads the context; it never lives in this module.
//
// No Vulkan instance is created here. Vulkan facts from an instance probe (flux::gfx::probe_vulkan,
// used by the CLI) may be passed in; otherwise only declarative evidence (properties, driver
// files) is used, with lower confidence.
#pragma once

#include "CapabilityContext.hpp"
#include "KernelIntelligence.hpp"
#include "VulkanProbe.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace flux::graphics {

inline constexpr const char *kDomain = "graphics";
inline constexpr const char *kPublisher = "graphics";

/// Everything graphics intelligence may look at. All optional; missing evidence -> Unknown.
struct GraphicsEvidence {
    std::function<std::string(const std::string &key)> property; // ro.* reader, "" when unset
    const flux::kernel::ReadOnlyFs *fs = nullptr;                  // for sysfs / vendor driver files
    std::optional<flux::gfx::VulkanFacts> vulkan;                  // only when an instance probe ran
    const flux::context::CapabilityContext *context = nullptr;     // kernel facts (GPU freq/load nodes)
};

/// Facts with ids "graphics.*" (see GRAPHICS_INTELLIGENCE.md for the list). Per-source vendor/model
/// facts ("graphics.gpu.vendor.<source>") are kept next to the resolved one, so a conflict stays
/// visible. Confidence: High / Medium / Low / None (= UNKNOWN).
std::vector<flux::context::CapabilityFact> observe(const GraphicsEvidence &evidence);

/// observe() + publish under kPublisher (snapshot replace).
void publish(const GraphicsEvidence &evidence, flux::context::CapabilityContext &context);

/// "3.2" from ro.opengles.version (major << 16 | minor); "" when invalid.
std::string format_gles_version(const std::string &property_value);

} // namespace flux::graphics
