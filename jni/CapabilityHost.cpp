/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CapabilityHost.hpp"

#include <sys/system_properties.h>

#include "DisplayIntelligence.hpp"
#include "GraphicsIntelligence.hpp"

#include <Exec.hpp>
#include <FluxLog.hpp>

namespace flux_capability {

namespace {

std::string property(const char *name) {
    char value[PROP_VALUE_MAX] = {};
    const int len = __system_property_get(name, value);
    return len > 0 ? std::string(value, static_cast<size_t>(len)) : std::string();
}

std::shared_ptr<flux::context::CapabilityContext> &shared() {
    static auto ctx = std::make_shared<flux::context::CapabilityContext>();
    return ctx;
}

flux::kernel::CapabilityBootstrap &bootstrapper() {
    static flux::kernel::CapabilityBootstrap boot(shared(), flux::kernel::make_device_probe("", [] {
        return flux::kernel::PlatformHint{property("ro.board.platform"), property("ro.hardware"),
                                          property("ro.soc.manufacturer")};
    }));
    return boot;
}

/// Graphics capability facts (Step 8). No Vulkan instance is created in fluxd: only properties,
/// driver files and the kernel's GPU facts. A failure logs and leaves graphics facts Unknown.
void publish_graphics() {
    try {
        auto fs = flux::kernel::make_readonly_fs("");
        flux::graphics::publish({[](const std::string &k) { return property(k.c_str()); }, fs.get(), std::nullopt,
                                 shared().get()},
                                *shared());
        const auto vendor = shared()->resolve("graphics.gpu.vendor");
        const auto vk = shared()->resolve("graphics.vulkan.available");
        LOGI_TAG("Capability", "graphics capabilities published: gpu {}, vulkan {}",
                 vendor.fact ? vendor.fact->value : "unknown", flux::context::to_string(vk.support));
    } catch (const std::exception &e) {
        LOGW_TAG("Capability", "graphics capability probe failed ({}); graphics facts stay unknown", e.what());
    } catch (...) {
        LOGW_TAG("Capability", "graphics capability probe failed; graphics facts stay unknown");
    }
}

std::optional<std::string> collect(const std::vector<std::string> &argv) {
    auto out = flux::capture(argv, 512 * 1024);
    if (out.empty()) return std::nullopt; // not collected -> Unknown, never "absent"
    return out;
}

/// Display and rendering capability facts (Step 8.5). Reads `dumpsys display`, `wm size` and
/// `service list`; changes nothing. A failure logs and leaves the facts Unknown.
void publish_display() {
    try {
        flux::display::publish({collect({"/system/bin/dumpsys", "display"}), collect({"/system/bin/wm", "size"}),
                                collect({"/system/bin/service", "list"}),
                                [](const std::string &k) { return property(k.c_str()); }, shared().get()},
                               *shared());
        const auto modes = shared()->resolve("display.refresh.modes");
        const auto sf = shared()->resolve("rendering.surfaceflinger");
        LOGI_TAG("Capability", "display capabilities published: refresh modes [{}], surfaceflinger {}",
                 modes.fact ? modes.fact->value : "unknown", flux::context::to_string(sf.support));
    } catch (const std::exception &e) {
        LOGW_TAG("Capability", "display capability probe failed ({}); display facts stay unknown", e.what());
    } catch (...) {
        LOGW_TAG("Capability", "display capability probe failed; display facts stay unknown");
    }
}

} // namespace

std::shared_ptr<const flux::context::CapabilityContext> context() { return shared(); }

const flux::kernel::BootstrapResult &bootstrap() {
    const auto r = bootstrapper().run();
    if (r.status == flux::kernel::BootstrapStatus::Failed) {
        LOGW_TAG("Capability", "kernel capability probe failed ({}); capabilities stay unknown", r.error);
    } else {
        const auto integ = shared()->resolve("kernel.integration");
        const auto adapter = shared()->resolve("kernel.adapter");
        LOGI_TAG("Capability", "kernel capabilities published: {} facts, {} supported, status {}, kernel {}, adapter {}",
                 r.facts, r.supported, flux::kernel::to_string(r.status),
                 integ.fact ? integ.fact->value : "unknown", adapter.fact ? adapter.fact->value : "unknown");
    }
    publish_graphics();
    publish_display();
    return bootstrapper().last();
}

} // namespace flux_capability
