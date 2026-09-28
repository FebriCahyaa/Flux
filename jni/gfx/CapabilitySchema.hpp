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

// C++ binding for the canonical capability model.
//
// SynthesisCore owns the model: schema/capability_schema_v4.json in that repo is
// authoritative and a byte-identical copy is vendored next to this header. The
// constants below must match that descriptor exactly; capability_schema_test.cpp
// parses the vendored JSON and fails the build if they drift.
//
// Collectors normalise what they observe into these domains and keys. Nothing in
// this layer interprets a capability or decides a backend.

namespace flux::gfx::schema {

/// Bumped only when a key is removed or changes meaning. Additions are in-place.
inline constexpr int kVersion = 4;

namespace domain {
inline constexpr const char *kVulkan = "vulkan";
inline constexpr const char *kGpu = "gpu";
inline constexpr const char *kDisplay = "display";
inline constexpr const char *kHwc = "hwc";
inline constexpr const char *kRenderEngine = "renderengine";
inline constexpr const char *kRuntime = "runtime";
} // namespace domain

namespace vulkan {
inline constexpr const char *kAvailable = "available";
inline constexpr const char *kStatus = "status";
inline constexpr const char *kDetail = "detail";
inline constexpr const char *kLoaderPresent = "loader_present";
inline constexpr const char *kInstanceVersion = "instance_version";
inline constexpr const char *kApiVersion = "api_version";
inline constexpr const char *kApiVersionMajor = "api_version_major";
inline constexpr const char *kApiVersionMinor = "api_version_minor";
inline constexpr const char *kApiVersionPatch = "api_version_patch";
inline constexpr const char *kDriverVersionRaw = "driver_version_raw";
inline constexpr const char *kDeviceCount = "device_count";
inline constexpr const char *kDeviceName = "device_name";
inline constexpr const char *kDeviceType = "device_type";
inline constexpr const char *kVendorId = "vendor_id";
inline constexpr const char *kDeviceId = "device_id";
inline constexpr const char *kInstanceExtensions = "instance_extensions";
inline constexpr const char *kDeviceExtensions = "device_extensions";
inline constexpr const char *kFeaturesSupported = "features_supported";

// vulkan.status values
inline constexpr const char *kStatusOk = "ok";
inline constexpr const char *kStatusAbsent = "absent";
inline constexpr const char *kStatusNoDevice = "no_device";
inline constexpr const char *kStatusError = "error";
} // namespace vulkan

namespace gpu {
inline constexpr const char *kVendor = "vendor";
inline constexpr const char *kFamily = "family";
inline constexpr const char *kModel = "model";
inline constexpr const char *kVendorId = "vendor_id";
inline constexpr const char *kDeviceId = "device_id";
inline constexpr const char *kDriverVersion = "driver_version";
inline constexpr const char *kKgslPresent = "kgsl_present";
inline constexpr const char *kDevfreqPresent = "devfreq_present";
} // namespace gpu

namespace display {
inline constexpr const char *kWidthPx = "width_px";
inline constexpr const char *kHeightPx = "height_px";
inline constexpr const char *kDensityDpi = "density_dpi";
inline constexpr const char *kRefreshRateHz = "refresh_rate_hz";
inline constexpr const char *kMinRefreshRateHz = "min_refresh_rate_hz";
inline constexpr const char *kPeakRefreshRateHz = "peak_refresh_rate_hz";
inline constexpr const char *kSupportedModes = "supported_modes";
inline constexpr const char *kHdrTypes = "hdr_types";
inline constexpr const char *kWideColorGamut = "wide_color_gamut";
} // namespace display

namespace hwc {
inline constexpr const char *kServicePresent = "service_present";
inline constexpr const char *kInterface = "interface";
inline constexpr const char *kComposerVersion = "composer_version";
inline constexpr const char *kVsyncPeriodNs = "vsync_period_ns";

inline constexpr const char *kInterfaceHidl = "hidl";
inline constexpr const char *kInterfaceAidl = "aidl";
inline constexpr const char *kInterfaceUnknown = "unknown";
} // namespace hwc

namespace renderengine {
inline constexpr const char *kBackend = "backend";
inline constexpr const char *kHwuiRenderer = "hwui_renderer";
inline constexpr const char *kGpuComposition = "gpu_composition";
} // namespace renderengine

namespace runtime {
inline constexpr const char *kAndroidSdk = "android_sdk";
inline constexpr const char *kKernelIsGki = "kernel_is_gki";
inline constexpr const char *kSocModel = "soc_model";
inline constexpr const char *kSocManufacturer = "soc_manufacturer";
inline constexpr const char *kAbi = "abi";
inline constexpr const char *kPageSize = "page_size";
} // namespace runtime

/// Status a collector reports for its own run, recorded in the sources array.
namespace source_status {
inline constexpr const char *kOk = "ok";
inline constexpr const char *kPartial = "partial";
inline constexpr const char *kUnavailable = "unavailable";
inline constexpr const char *kError = "error";
} // namespace source_status

} // namespace flux::gfx::schema
