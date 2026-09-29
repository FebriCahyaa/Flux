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

#include <cstdint>
#include <string>
#include <vector>

#include "CapabilityCollector.hpp"

namespace flux::gfx {

/**
 * @brief Raw Vulkan observations, free of any Vulkan type.
 *
 * Keeping the transport's output in a plain struct is what makes the probe
 * testable: normalisation is a pure function of this struct, so every device
 * shape (no loader, loader but no device, Adreno, Mali, a driver that reports a
 * version the loader does not) can be exercised on the build machine without a
 * GPU. Only VulkanProbe.cpp ever touches libvulkan.
 */
struct VulkanFacts {
    /// True only when an instance was created and a physical device was queried.
    bool available = false;

    /// schema::vulkan::kStatus* — why, when available is false.
    std::string status;
    std::string detail;

    /// dlopen on the loader succeeded, regardless of whether a device turned up.
    bool loader_present = false;

    /// vkEnumerateInstanceVersion, 0 when the loader predates Vulkan 1.1.
    uint32_t instance_version_raw = 0;

    /// VkPhysicalDeviceProperties identity fields.
    uint32_t api_version_raw = 0;
    uint32_t driver_version_raw = 0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint32_t device_type_raw = 0;
    std::string device_name;

    /// How many physical devices the instance enumerated; facts describe the first.
    uint32_t device_count = 0;

    std::vector<std::string> instance_extensions;
    std::vector<std::string> device_extensions;

    /// Names of the VkPhysicalDeviceFeatures members reported as supported.
    std::vector<std::string> features_supported;
};

/**
 * @brief Query the Vulkan loader for observable facts.
 *
 * Never throws, never aborts, and returns promptly on a device with no Vulkan:
 * a missing libvulkan is an ordinary result, not an error. Facts only — the probe
 * forms no opinion about which backend should be used.
 */
VulkanFacts probe_vulkan();

// -- pure helpers, exercised directly by the host tests ---------------------

/// "1.3.128" from a packed VK_MAKE_VERSION value.
std::string format_vulkan_version(uint32_t packed);

uint32_t vulkan_version_major(uint32_t packed);
uint32_t vulkan_version_minor(uint32_t packed);
uint32_t vulkan_version_patch(uint32_t packed);

/// Canonical schema::gpu::kVendor value for a PCI vendor id.
std::string vendor_name_for_id(uint32_t vendor_id);

/// Canonical schema::gpu::kFamily value, from the vendor id and reported device name.
std::string gpu_family_for(uint32_t vendor_id, const std::string &device_name);

/// Canonical schema::vulkan::kDeviceType value for a VkPhysicalDeviceType.
std::string device_type_name(uint32_t device_type_raw);

/**
 * @brief Write @p facts into @p out as canonical vulkan.* and gpu.* facts.
 *
 * Pure: no device access. When facts.available is false only the availability
 * keys are written, so absent stays absent rather than becoming a zero.
 */
void normalize_vulkan(const VulkanFacts &facts, CapabilityModel &out, const std::string &source_id);

/// Collector wrapper around probe_vulkan() + normalize_vulkan().
class VulkanCollector : public CapabilityCollector {
public:
    std::string id() const override { return "fluxd.vulkan"; }
    std::vector<std::string> domains() const override;
    CapabilitySource collect(CapabilityModel &out) override;
};

} // namespace flux::gfx
