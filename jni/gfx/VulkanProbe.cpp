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

#include "VulkanProbe.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <dlfcn.h>

namespace flux::gfx {

// ---------------------------------------------------------------------------
// Minimal Vulkan ABI
//
// The probe resolves everything through dlopen/dlsym, so it deliberately does
// not include <vulkan/vulkan.h>: the daemon then builds identically whether or
// not Vulkan headers are installed, on device and on the build machine alike.
// Only the frozen Vulkan 1.0 core layouts are declared, and only as far as the
// fields actually read. VkPhysicalDeviceProperties and VkPhysicalDeviceFeatures
// are filled by the driver into oversized zeroed buffers, so the tail this file
// does not declare (limits, sparse properties) is written to memory we own.
// ---------------------------------------------------------------------------
namespace {

using VkBool32 = uint32_t;
using VkResult = int32_t;
using VkInstance = void *;
using VkPhysicalDevice = void *;

constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_INCOMPLETE = 5;
/// No ICD the loader can use. A loader stub with no driver behind it is an
/// ordinary device without Vulkan, not a broken one.
constexpr VkResult VK_ERROR_INCOMPATIBLE_DRIVER = -9;

constexpr uint32_t VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
constexpr uint32_t VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;

constexpr size_t VK_MAX_EXTENSION_NAME_SIZE = 256;
constexpr size_t VK_MAX_PHYSICAL_DEVICE_NAME_SIZE = 256;
constexpr size_t VK_UUID_SIZE = 16;

/// Room for the whole of VkPhysicalDeviceProperties (~824 bytes) and of
/// VkPhysicalDeviceFeatures (220 bytes) on any implementation.
constexpr size_t kPropertiesBufferSize = 2048;
constexpr size_t kFeaturesBufferSize = 512;

struct VkApplicationInfo {
    uint32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
};

struct VkExtensionProperties {
    char extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
};

/// The identity prefix of VkPhysicalDeviceProperties; limits follow at offset 292.
struct VkPhysicalDevicePropertiesHead {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t pipelineCacheUUID[VK_UUID_SIZE];
};

using PFN_vkVoidFunction = void (*)();
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstance, const char *);
using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo *, const void *, VkInstance *);
using PFN_vkDestroyInstance = void (*)(VkInstance, const void *);
using PFN_vkEnumerateInstanceVersion = VkResult (*)(uint32_t *);
using PFN_vkEnumerateInstanceExtensionProperties = VkResult (*)(const char *, uint32_t *, VkExtensionProperties *);
using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t *, VkPhysicalDevice *);
using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, void *);
using PFN_vkGetPhysicalDeviceFeatures = void (*)(VkPhysicalDevice, void *);
using PFN_vkEnumerateDeviceExtensionProperties = VkResult (*)(VkPhysicalDevice, const char *, uint32_t *,
                                                              VkExtensionProperties *);

/// VkPhysicalDeviceFeatures in declaration order: 55 VkBool32 members, frozen since 1.0.
constexpr std::array<const char *, 55> kFeatureNames = {
    "robustBufferAccess",
    "fullDrawIndexUint32",
    "imageCubeArray",
    "independentBlend",
    "geometryShader",
    "tessellationShader",
    "sampleRateShading",
    "dualSrcBlend",
    "logicOp",
    "multiDrawIndirect",
    "drawIndirectFirstInstance",
    "depthClamp",
    "depthBiasClamp",
    "fillModeNonSolid",
    "depthBounds",
    "wideLines",
    "largePoints",
    "alphaToOne",
    "multiViewport",
    "samplerAnisotropy",
    "textureCompressionETC2",
    "textureCompressionASTC_LDR",
    "textureCompressionBC",
    "occlusionQueryPrecise",
    "pipelineStatisticsQuery",
    "vertexPipelineStoresAndAtomics",
    "fragmentStoresAndAtomics",
    "shaderTessellationAndGeometryPointSize",
    "shaderImageGatherExtended",
    "shaderStorageImageExtendedFormats",
    "shaderStorageImageMultisample",
    "shaderStorageImageReadWithoutFormat",
    "shaderStorageImageWriteWithoutFormat",
    "shaderUniformBufferArrayDynamicIndexing",
    "shaderSampledImageArrayDynamicIndexing",
    "shaderStorageBufferArrayDynamicIndexing",
    "shaderStorageImageArrayDynamicIndexing",
    "shaderClipDistance",
    "shaderCullDistance",
    "shaderFloat64",
    "shaderInt64",
    "shaderInt16",
    "shaderResourceResidency",
    "shaderResourceMinLod",
    "sparseBinding",
    "sparseResidencyBuffer",
    "sparseResidencyImage2D",
    "sparseResidencyImage3D",
    "sparseResidency2Samples",
    "sparseResidency4Samples",
    "sparseResidency8Samples",
    "sparseResidency16Samples",
    "sparseResidencyAliased",
    "variableMultisampleRate",
    "inheritedQueries",
};

/// Loader sonames: Android ships the first, desktop Linux the others.
constexpr std::array<const char *, 3> kLoaderNames = {"libvulkan.so", "libvulkan.so.1", "libvulkan.so.0"};

/// Copies a fixed-size driver-supplied char array without trusting it to be terminated.
std::string bounded_string(const char *data, size_t capacity) {
    size_t len = 0;
    while (len < capacity && data[len] != '\0') ++len;
    return std::string(data, len);
}

template <typename Fn>
Fn load_instance_fn(PFN_vkGetInstanceProcAddr get_proc, VkInstance instance, const char *name) {
    return reinterpret_cast<Fn>(get_proc(instance, name));
}

} // namespace

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

uint32_t vulkan_version_major(uint32_t packed) { return packed >> 22; }
uint32_t vulkan_version_minor(uint32_t packed) { return (packed >> 12) & 0x3ffu; }
uint32_t vulkan_version_patch(uint32_t packed) { return packed & 0xfffu; }

std::string format_vulkan_version(uint32_t packed) {
    return std::to_string(vulkan_version_major(packed)) + "." + std::to_string(vulkan_version_minor(packed)) + "." +
           std::to_string(vulkan_version_patch(packed));
}

std::string vendor_name_for_id(uint32_t vendor_id) {
    switch (vendor_id) {
    case 0x5143: return "qualcomm";
    case 0x13B5: return "arm";
    case 0x1010: return "imagination";
    case 0x10DE: return "nvidia";
    case 0x1002: return "amd";
    case 0x8086: return "intel";
    case 0x14E4: return "broadcom";
    case 0x10001: return "vivante"; // Khronos-allocated vendor id
    default: return "unknown";
    }
}

std::string gpu_family_for(uint32_t vendor_id, const std::string &device_name) {
    switch (vendor_id) {
    case 0x5143: return "adreno";
    case 0x13B5: return "mali";
    case 0x1010: return "powervr";
    case 0x10DE: return "geforce";
    case 0x1002: return "radeon";
    case 0x8086: return "intel";
    case 0x14E4: return "videocore";
    case 0x10001: return "vivante";
    default: break;
    }

    // An unknown vendor id still usually names its family in the device string.
    std::string lower = device_name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("adreno") != std::string::npos) return "adreno";
    if (lower.find("mali") != std::string::npos) return "mali";
    if (lower.find("powervr") != std::string::npos) return "powervr";
    if (lower.find("radeon") != std::string::npos) return "radeon";
    if (lower.find("geforce") != std::string::npos) return "geforce";
    if (lower.find("videocore") != std::string::npos) return "videocore";
    if (lower.find("vivante") != std::string::npos) return "vivante";
    return "unknown";
}

std::string device_type_name(uint32_t device_type_raw) {
    switch (device_type_raw) {
    case 0: return "other";
    case 1: return "integrated";
    case 2: return "discrete";
    case 3: return "virtual";
    case 4: return "cpu";
    default: return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

VulkanFacts probe_vulkan() {
    VulkanFacts facts;
    facts.status = schema::vulkan::kStatusAbsent;

    void *lib = nullptr;
    for (const char *name : kLoaderNames) {
        lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (lib) break;
    }
    if (!lib) {
        facts.detail = "libvulkan not present";
        return facts;
    }
    facts.loader_present = true;

    // dlclose on every return path below.
    struct LibGuard {
        void *handle;
        ~LibGuard() {
            if (handle) dlclose(handle);
        }
    } guard{lib};

    auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    if (!get_proc) {
        facts.status = schema::vulkan::kStatusError;
        facts.detail = "vkGetInstanceProcAddr missing from loader";
        return facts;
    }

    // Instance-version reporting arrived in Vulkan 1.1; absent means a 1.0 loader.
    if (auto enumerate_version =
            load_instance_fn<PFN_vkEnumerateInstanceVersion>(get_proc, nullptr, "vkEnumerateInstanceVersion")) {
        uint32_t version = 0;
        if (enumerate_version(&version) == VK_SUCCESS) facts.instance_version_raw = version;
    }
    if (facts.instance_version_raw == 0) facts.instance_version_raw = (1u << 22); // 1.0.0

    if (auto enumerate_instance_ext = load_instance_fn<PFN_vkEnumerateInstanceExtensionProperties>(
            get_proc, nullptr, "vkEnumerateInstanceExtensionProperties")) {
        uint32_t count = 0;
        if (enumerate_instance_ext(nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
            std::vector<VkExtensionProperties> props(count);
            VkResult r = enumerate_instance_ext(nullptr, &count, props.data());
            if (r == VK_SUCCESS || r == VK_INCOMPLETE) {
                for (uint32_t i = 0; i < count && i < props.size(); ++i) {
                    facts.instance_extensions.push_back(
                        bounded_string(props[i].extensionName, VK_MAX_EXTENSION_NAME_SIZE));
                }
            }
        }
    }

    auto create_instance = load_instance_fn<PFN_vkCreateInstance>(get_proc, nullptr, "vkCreateInstance");
    if (!create_instance) {
        facts.status = schema::vulkan::kStatusError;
        facts.detail = "vkCreateInstance unresolved";
        return facts;
    }

    // Request 1.0 so the call is accepted by every driver; the device still
    // reports the API version it actually implements.
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "fluxd-capability-probe";
    app.applicationVersion = 1;
    app.pEngineName = "FluxGFX";
    app.engineVersion = 1;
    app.apiVersion = (1u << 22);

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &app;

    VkInstance instance = nullptr;
    VkResult result = create_instance(&create, nullptr, &instance);
    if (result != VK_SUCCESS || !instance) {
        // A loader with no driver behind it means this device has no usable
        // Vulkan — the same answer as having no loader, reached a step later.
        // Reporting it as an error would make an ordinary device look broken.
        facts.status = result == VK_ERROR_INCOMPATIBLE_DRIVER ? schema::vulkan::kStatusNoDevice
                                                              : schema::vulkan::kStatusError;
        facts.detail = "vkCreateInstance failed (VkResult " + std::to_string(result) + ")";
        return facts;
    }

    auto destroy_instance = load_instance_fn<PFN_vkDestroyInstance>(get_proc, instance, "vkDestroyInstance");
    struct InstanceGuard {
        PFN_vkDestroyInstance destroy;
        VkInstance instance;
        ~InstanceGuard() {
            if (destroy && instance) destroy(instance, nullptr);
        }
    } instance_guard{destroy_instance, instance};

    auto enumerate_devices =
        load_instance_fn<PFN_vkEnumeratePhysicalDevices>(get_proc, instance, "vkEnumeratePhysicalDevices");
    auto get_properties =
        load_instance_fn<PFN_vkGetPhysicalDeviceProperties>(get_proc, instance, "vkGetPhysicalDeviceProperties");
    if (!enumerate_devices || !get_properties) {
        facts.status = schema::vulkan::kStatusError;
        facts.detail = "physical-device entry points unresolved";
        return facts;
    }

    uint32_t device_count = 0;
    result = enumerate_devices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        facts.status = schema::vulkan::kStatusNoDevice;
        facts.detail = "no Vulkan physical device";
        return facts;
    }
    facts.device_count = device_count;

    std::vector<VkPhysicalDevice> devices(device_count);
    result = enumerate_devices(instance, &device_count, devices.data());
    if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || device_count == 0) {
        facts.status = schema::vulkan::kStatusNoDevice;
        facts.detail = "physical-device enumeration failed";
        return facts;
    }

    // Android exposes exactly one GPU; where several exist the first is the
    // one SurfaceFlinger uses. Reporting the rest is a later-phase concern.
    VkPhysicalDevice device = devices[0];

    alignas(8) unsigned char prop_buf[kPropertiesBufferSize] = {};
    get_properties(device, prop_buf);
    const auto *props = reinterpret_cast<const VkPhysicalDevicePropertiesHead *>(prop_buf);

    facts.api_version_raw = props->apiVersion;
    facts.driver_version_raw = props->driverVersion;
    facts.vendor_id = props->vendorID;
    facts.device_id = props->deviceID;
    facts.device_type_raw = props->deviceType;
    facts.device_name = bounded_string(props->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);

    if (auto get_features =
            load_instance_fn<PFN_vkGetPhysicalDeviceFeatures>(get_proc, instance, "vkGetPhysicalDeviceFeatures")) {
        alignas(8) unsigned char feature_buf[kFeaturesBufferSize] = {};
        get_features(device, feature_buf);
        const auto *flags = reinterpret_cast<const VkBool32 *>(feature_buf);
        for (size_t i = 0; i < kFeatureNames.size(); ++i) {
            if (flags[i]) facts.features_supported.emplace_back(kFeatureNames[i]);
        }
    }

    if (auto enumerate_device_ext = load_instance_fn<PFN_vkEnumerateDeviceExtensionProperties>(
            get_proc, instance, "vkEnumerateDeviceExtensionProperties")) {
        uint32_t count = 0;
        if (enumerate_device_ext(device, nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
            std::vector<VkExtensionProperties> props_list(count);
            VkResult r = enumerate_device_ext(device, nullptr, &count, props_list.data());
            if (r == VK_SUCCESS || r == VK_INCOMPLETE) {
                for (uint32_t i = 0; i < count && i < props_list.size(); ++i) {
                    facts.device_extensions.push_back(
                        bounded_string(props_list[i].extensionName, VK_MAX_EXTENSION_NAME_SIZE));
                }
            }
        }
    }

    // Stable ordering keeps the serialised model diffable between runs.
    std::sort(facts.instance_extensions.begin(), facts.instance_extensions.end());
    std::sort(facts.device_extensions.begin(), facts.device_extensions.end());

    facts.available = true;
    facts.status = schema::vulkan::kStatusOk;
    return facts;
}

// ---------------------------------------------------------------------------
// Normalisation
// ---------------------------------------------------------------------------

void normalize_vulkan(const VulkanFacts &facts, CapabilityModel &out, const std::string &source_id) {
    namespace vk = schema::vulkan;

    out.set(schema::domain::kVulkan, vk::kAvailable, facts.available, source_id);
    out.set(schema::domain::kVulkan, vk::kLoaderPresent, facts.loader_present, source_id);
    out.set(schema::domain::kVulkan, vk::kStatus, facts.status, source_id);
    if (!facts.detail.empty()) out.set(schema::domain::kVulkan, vk::kDetail, facts.detail, source_id);

    // The loader answers the instance version even when no device turns up, so it
    // is a real observation whenever the library loaded.
    if (facts.loader_present && facts.instance_version_raw != 0) {
        out.set(schema::domain::kVulkan, vk::kInstanceVersion, format_vulkan_version(facts.instance_version_raw),
                source_id);
    }
    if (!facts.instance_extensions.empty()) {
        out.set(schema::domain::kVulkan, vk::kInstanceExtensions, facts.instance_extensions, source_id);
    }

    // Without a device the remaining fields were never observed. Leaving them
    // absent is the contract: a consumer must not read a zero vendor id as Intel.
    if (!facts.available) return;

    out.set(schema::domain::kVulkan, vk::kApiVersion, format_vulkan_version(facts.api_version_raw), source_id);
    out.set(schema::domain::kVulkan, vk::kApiVersionMajor,
            static_cast<int64_t>(vulkan_version_major(facts.api_version_raw)), source_id);
    out.set(schema::domain::kVulkan, vk::kApiVersionMinor,
            static_cast<int64_t>(vulkan_version_minor(facts.api_version_raw)), source_id);
    out.set(schema::domain::kVulkan, vk::kApiVersionPatch,
            static_cast<int64_t>(vulkan_version_patch(facts.api_version_raw)), source_id);
    out.set(schema::domain::kVulkan, vk::kDriverVersionRaw, static_cast<int64_t>(facts.driver_version_raw), source_id);
    out.set(schema::domain::kVulkan, vk::kDeviceCount, static_cast<int64_t>(facts.device_count), source_id);
    out.set(schema::domain::kVulkan, vk::kDeviceName, facts.device_name, source_id);
    out.set(schema::domain::kVulkan, vk::kDeviceType, device_type_name(facts.device_type_raw), source_id);
    out.set(schema::domain::kVulkan, vk::kVendorId, static_cast<int64_t>(facts.vendor_id), source_id);
    out.set(schema::domain::kVulkan, vk::kDeviceId, static_cast<int64_t>(facts.device_id), source_id);

    if (!facts.device_extensions.empty()) {
        out.set(schema::domain::kVulkan, vk::kDeviceExtensions, facts.device_extensions, source_id);
    }
    if (!facts.features_supported.empty()) {
        out.set(schema::domain::kVulkan, vk::kFeaturesSupported, facts.features_supported, source_id);
    }

    // GPU identity is the same observation expressed in the vendor-neutral domain,
    // so consumers that only need "which GPU" never parse Vulkan specifics.
    namespace gpu = schema::gpu;
    out.set(schema::domain::kGpu, gpu::kVendor, vendor_name_for_id(facts.vendor_id), source_id);
    out.set(schema::domain::kGpu, gpu::kFamily, gpu_family_for(facts.vendor_id, facts.device_name), source_id);
    out.set(schema::domain::kGpu, gpu::kModel, facts.device_name, source_id);
    out.set(schema::domain::kGpu, gpu::kVendorId, static_cast<int64_t>(facts.vendor_id), source_id);
    out.set(schema::domain::kGpu, gpu::kDeviceId, static_cast<int64_t>(facts.device_id), source_id);
    out.set(schema::domain::kGpu, gpu::kDriverVersion, format_vulkan_version(facts.driver_version_raw), source_id);
}

std::vector<std::string> VulkanCollector::domains() const {
    return {schema::domain::kVulkan, schema::domain::kGpu};
}

CapabilitySource VulkanCollector::collect(CapabilityModel &out) {
    CapabilitySource source;
    source.id = id();
    source.domains = domains();
    source.collected_at_ms = now_ms();

    const VulkanFacts facts = probe_vulkan();
    normalize_vulkan(facts, out, source.id);

    if (facts.available) {
        source.status = schema::source_status::kOk;
    } else if (facts.status == schema::vulkan::kStatusError) {
        source.status = schema::source_status::kError;
        source.detail = facts.detail;
    } else {
        // No Vulkan on this device is a normal answer, not a failure to report.
        source.status = schema::source_status::kUnavailable;
        source.detail = facts.detail;
    }
    return source;
}

} // namespace flux::gfx
