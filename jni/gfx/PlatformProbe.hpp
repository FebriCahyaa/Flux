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

#pragma once

#include <functional>
#include <string>

#include "CapabilityCollector.hpp"

namespace flux::gfx {

/**
 * @brief The device lookups the platform collectors need, behind function objects.
 *
 * Injecting the lookups is what lets every collector below run against a fake
 * device on the build machine: the host tests supply a table of properties and
 * paths and assert on the facts that come out. On device, default_system_query()
 * binds to the real property store and filesystem.
 *
 * Each accessor returns an empty string when the value does not exist, so a
 * collector can tell "not set" from "set to something" without an errno dance.
 */
struct SystemQuery {
    std::function<std::string(const std::string &key)> get_property;
    std::function<bool(const std::string &path)> path_exists;
    std::function<std::string(const std::string &path)> read_file;
};

/// Binds to __system_property_get (device) or getenv-free stubs (host).
SystemQuery default_system_query();

/**
 * @brief Framework-owned display facts, collected natively where observable.
 *
 * SurfaceFlinger is the authority on display modes, so the richest facts come
 * from a SynthesisCore framework provider. What fluxd can see without the
 * framework — the configured refresh range and panel geometry as the ROM
 * advertises them — is collected here, and a framework contribution later fills
 * the rest. Keys already present are never overwritten by a merge.
 */
class DisplayCollector : public CapabilityCollector {
public:
    explicit DisplayCollector(SystemQuery query) : query_(std::move(query)) {}
    std::string id() const override { return "fluxd.display"; }
    std::vector<std::string> domains() const override;
    CapabilitySource collect(CapabilityModel &out) override;

private:
    SystemQuery query_;
};

/**
 * @brief Hardware composer presence and interface generation.
 *
 * Presence is inferred from the composer HAL binary the ROM ships: an AIDL
 * service implies Composer 3+, a HIDL one the @2.x line. The precise composer
 * version needs a binder call the framework is better placed to make, so it is
 * left absent here rather than guessed.
 */
class HwcCollector : public CapabilityCollector {
public:
    explicit HwcCollector(SystemQuery query) : query_(std::move(query)) {}
    std::string id() const override { return "fluxd.hwc"; }
    std::vector<std::string> domains() const override;
    CapabilitySource collect(CapabilityModel &out) override;

private:
    SystemQuery query_;
};

/**
 * @brief The RenderEngine and HWUI backends as currently configured.
 *
 * Reports what the device is set to. It does not set, prefer or recommend a
 * backend: selection is a later phase and belongs above the capability layer.
 */
class RenderEngineCollector : public CapabilityCollector {
public:
    explicit RenderEngineCollector(SystemQuery query) : query_(std::move(query)) {}
    std::string id() const override { return "fluxd.renderengine"; }
    std::vector<std::string> domains() const override;
    CapabilitySource collect(CapabilityModel &out) override;

private:
    SystemQuery query_;
};

/** @brief Platform and kernel facts that gate what the graphics stack may attempt. */
class RuntimeCollector : public CapabilityCollector {
public:
    explicit RuntimeCollector(SystemQuery query) : query_(std::move(query)) {}
    std::string id() const override { return "fluxd.runtime"; }
    std::vector<std::string> domains() const override;
    CapabilitySource collect(CapabilityModel &out) override;

private:
    SystemQuery query_;
};

/** @brief GPU facts observable from sysfs, independent of Vulkan. */
class GpuSysfsCollector : public CapabilityCollector {
public:
    explicit GpuSysfsCollector(SystemQuery query) : query_(std::move(query)) {}
    std::string id() const override { return "fluxd.gpu_sysfs"; }
    std::vector<std::string> domains() const override;
    CapabilitySource collect(CapabilityModel &out) override;

private:
    SystemQuery query_;
};

/// True when @p release names a GKI kernel (an `-androidNN-` segment).
bool kernel_release_is_gki(const std::string &release);

/// Parses a refresh rate written as a settings value ("120.0"); 0 when unparsable.
double parse_refresh_rate(const std::string &value);

} // namespace flux::gfx
