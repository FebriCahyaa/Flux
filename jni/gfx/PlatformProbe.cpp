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

#include "PlatformProbe.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace flux::gfx {

namespace {

/// Composer HAL binaries, newest interface first: the first match decides the generation.
struct ComposerPath {
    const char *path;
    const char *interface;
    const char *version;
};

constexpr ComposerPath kComposerPaths[] = {
    {"/vendor/bin/hw/android.hardware.graphics.composer3-service", schema::hwc::kInterfaceAidl, "3"},
    {"/vendor/bin/hw/android.hardware.composer.hwc3-service.qti", schema::hwc::kInterfaceAidl, "3"},
    {"/vendor/bin/hw/android.hardware.graphics.composer@2.4-service", schema::hwc::kInterfaceHidl, "2.4"},
    {"/vendor/bin/hw/android.hardware.graphics.composer@2.3-service", schema::hwc::kInterfaceHidl, "2.3"},
    {"/vendor/bin/hw/android.hardware.graphics.composer@2.2-service", schema::hwc::kInterfaceHidl, "2.2"},
    {"/vendor/bin/hw/android.hardware.graphics.composer@2.1-service", schema::hwc::kInterfaceHidl, "2.1"},
};

std::string trim(const std::string &s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

/// Writes @p value under @p domain / @p key only when it was actually observed.
void set_if_present(CapabilityModel &out, const std::string &domain, const char *key, const std::string &value,
                    const std::string &source) {
    if (value.empty()) return;
    out.set(domain, key, value, source);
}

std::optional<int64_t> parse_int(const std::string &value) {
    const std::string t = trim(value);
    if (t.empty()) return std::nullopt;
    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(t.c_str(), &end, 10);
    if (errno != 0 || end == t.c_str() || *end != '\0') return std::nullopt;
    return static_cast<int64_t>(parsed);
}

} // namespace

bool kernel_release_is_gki(const std::string &release) {
    // GKI releases carry an "-androidNN-" segment, e.g. 5.10.198-android12-9-...
    const size_t at = release.find("-android");
    if (at == std::string::npos) return false;
    size_t i = at + 8; // past "-android"
    size_t digits = 0;
    while (i < release.size() && std::isdigit(static_cast<unsigned char>(release[i]))) {
        ++i;
        ++digits;
    }
    return digits > 0 && i < release.size() && release[i] == '-';
}

double parse_refresh_rate(const std::string &value) {
    const std::string t = trim(value);
    if (t.empty()) return 0.0;
    errno = 0;
    char *end = nullptr;
    const double parsed = std::strtod(t.c_str(), &end);
    // Require the whole value to be the number: "120hz" is a malformed property,
    // and reading it as 120 would turn a ROM's typo into a fact.
    if (errno != 0 || end == t.c_str() || *end != '\0' || parsed <= 0.0) return 0.0;
    return parsed;
}

SystemQuery default_system_query() {
    SystemQuery q;

    q.get_property = [](const std::string &key) -> std::string {
#if defined(__ANDROID__)
        char value[PROP_VALUE_MAX] = {};
        const int len = __system_property_get(key.c_str(), value);
        return len > 0 ? std::string(value, static_cast<size_t>(len)) : std::string();
#else
        // No property store off device; collectors then report the key as absent.
        (void)key;
        return std::string();
#endif
    };

    q.path_exists = [](const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0;
    };

    q.read_file = [](const std::string &path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) return std::string();
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    return q;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

std::vector<std::string> DisplayCollector::domains() const { return {schema::domain::kDisplay}; }

CapabilitySource DisplayCollector::collect(CapabilityModel &out) {
    CapabilitySource source;
    source.id = id();
    source.domains = domains();
    source.collected_at_ms = now_ms();

    size_t observed = 0;

    // The ROM advertises its panel range through SurfaceFlinger's read-only props.
    const double min_rate = parse_refresh_rate(query_.get_property("ro.surface_flinger.min_refresh_rate"));
    if (min_rate > 0.0) {
        out.set(schema::domain::kDisplay, schema::display::kMinRefreshRateHz, min_rate, source.id);
        ++observed;
    }

    const double peak_rate = parse_refresh_rate(query_.get_property("ro.surface_flinger.max_refresh_rate"));
    if (peak_rate > 0.0) {
        out.set(schema::domain::kDisplay, schema::display::kPeakRefreshRateHz, peak_rate, source.id);
        ++observed;
    }

    const double default_rate = parse_refresh_rate(query_.get_property("ro.surface_flinger.default_peak_refresh_rate"));
    if (default_rate > 0.0) {
        out.set(schema::domain::kDisplay, schema::display::kRefreshRateHz, default_rate, source.id);
        ++observed;
    }

    if (query_.get_property("ro.surface_flinger.has_wide_color_display") == "true") {
        out.set(schema::domain::kDisplay, schema::display::kWideColorGamut, true, source.id);
        ++observed;
    } else if (query_.get_property("ro.surface_flinger.has_wide_color_display") == "false") {
        out.set(schema::domain::kDisplay, schema::display::kWideColorGamut, false, source.id);
        ++observed;
    }

    if (auto density = parse_int(query_.get_property("ro.sf.lcd_density"))) {
        out.set(schema::domain::kDisplay, schema::display::kDensityDpi, *density, source.id);
        ++observed;
    }

    // Geometry and the full mode list come from SurfaceFlinger; a framework
    // provider fills width_px/height_px/supported_modes/hdr_types later.
    if (observed == 0) {
        source.status = schema::source_status::kUnavailable;
        source.detail = "no display properties readable";
    } else {
        source.status = schema::source_status::kPartial;
        source.detail = "geometry and mode list need a framework provider";
    }
    return source;
}

// ---------------------------------------------------------------------------
// Hardware composer
// ---------------------------------------------------------------------------

std::vector<std::string> HwcCollector::domains() const { return {schema::domain::kHwc}; }

CapabilitySource HwcCollector::collect(CapabilityModel &out) {
    CapabilitySource source;
    source.id = id();
    source.domains = domains();
    source.collected_at_ms = now_ms();

    for (const auto &candidate : kComposerPaths) {
        if (!query_.path_exists(candidate.path)) continue;
        out.set(schema::domain::kHwc, schema::hwc::kServicePresent, true, source.id);
        out.set(schema::domain::kHwc, schema::hwc::kInterface, std::string(candidate.interface), source.id);
        out.set(schema::domain::kHwc, schema::hwc::kComposerVersion, std::string(candidate.version), source.id);
        source.status = schema::source_status::kPartial;
        source.detail = "vsync period needs a framework provider";
        return source;
    }

    // Absence of the binary is weak evidence: a ROM may bundle the composer into
    // another process. Record that nothing was found without asserting there is
    // no composer, so a framework provider can still answer authoritatively.
    out.set(schema::domain::kHwc, schema::hwc::kInterface, std::string(schema::hwc::kInterfaceUnknown), source.id);
    source.status = schema::source_status::kUnavailable;
    source.detail = "no composer HAL binary at a known path";
    return source;
}

// ---------------------------------------------------------------------------
// RenderEngine
// ---------------------------------------------------------------------------

std::vector<std::string> RenderEngineCollector::domains() const { return {schema::domain::kRenderEngine}; }

CapabilitySource RenderEngineCollector::collect(CapabilityModel &out) {
    CapabilitySource source;
    source.id = id();
    source.domains = domains();
    source.collected_at_ms = now_ms();

    size_t observed = 0;

    const std::string backend = query_.get_property("debug.renderengine.backend");
    if (!backend.empty()) {
        out.set(schema::domain::kRenderEngine, schema::renderengine::kBackend, backend, source.id);
        ++observed;
    }

    const std::string hwui = query_.get_property("debug.hwui.renderer");
    if (!hwui.empty()) {
        out.set(schema::domain::kRenderEngine, schema::renderengine::kHwuiRenderer, hwui, source.id);
        ++observed;
    }

    const std::string gpu_composition = query_.get_property("debug.sf.disable_hwc");
    if (!gpu_composition.empty()) {
        out.set(schema::domain::kRenderEngine, schema::renderengine::kGpuComposition, gpu_composition == "1",
                source.id);
        ++observed;
    }

    if (observed == 0) {
        source.status = schema::source_status::kUnavailable;
        source.detail = "no renderengine properties set; the platform default is in use";
    }
    return source;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------

std::vector<std::string> RuntimeCollector::domains() const { return {schema::domain::kRuntime}; }

CapabilitySource RuntimeCollector::collect(CapabilityModel &out) {
    CapabilitySource source;
    source.id = id();
    source.domains = domains();
    source.collected_at_ms = now_ms();

    if (auto sdk = parse_int(query_.get_property("ro.build.version.sdk"))) {
        out.set(schema::domain::kRuntime, schema::runtime::kAndroidSdk, *sdk, source.id);
    }
    set_if_present(out, schema::domain::kRuntime, schema::runtime::kSocModel,
                   query_.get_property("ro.soc.model"), source.id);
    set_if_present(out, schema::domain::kRuntime, schema::runtime::kSocManufacturer,
                   query_.get_property("ro.soc.manufacturer"), source.id);
    set_if_present(out, schema::domain::kRuntime, schema::runtime::kAbi,
                   query_.get_property("ro.product.cpu.abi"), source.id);

    // Page size is a property of this process, always observable.
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0) {
        out.set(schema::domain::kRuntime, schema::runtime::kPageSize, static_cast<int64_t>(page_size), source.id);
    }

    struct utsname uts {};
    if (uname(&uts) == 0) {
        out.set(schema::domain::kRuntime, schema::runtime::kKernelIsGki, kernel_release_is_gki(uts.release), source.id);
    }

    source.status = schema::source_status::kOk;
    return source;
}

// ---------------------------------------------------------------------------
// GPU sysfs
// ---------------------------------------------------------------------------

std::vector<std::string> GpuSysfsCollector::domains() const { return {schema::domain::kGpu}; }

CapabilitySource GpuSysfsCollector::collect(CapabilityModel &out) {
    CapabilitySource source;
    source.id = id();
    source.domains = domains();
    source.collected_at_ms = now_ms();

    const bool kgsl = query_.path_exists("/sys/class/kgsl/kgsl-3d0");
    out.set(schema::domain::kGpu, schema::gpu::kKgslPresent, kgsl, source.id);

    const bool devfreq = query_.path_exists("/sys/class/kgsl/kgsl-3d0/devfreq") ||
                         query_.path_exists("/sys/class/devfreq");
    out.set(schema::domain::kGpu, schema::gpu::kDevfreqPresent, devfreq, source.id);

    // Identity is the Vulkan collector's to give; only fill it in as a fallback
    // when Vulkan could not answer, so a merge never downgrades a better fact.
    if (kgsl && !out.has(schema::domain::kGpu, schema::gpu::kVendor)) {
        out.set(schema::domain::kGpu, schema::gpu::kVendor, std::string("qualcomm"), source.id);
        out.set(schema::domain::kGpu, schema::gpu::kFamily, std::string("adreno"), source.id);
    }

    source.status = schema::source_status::kOk;
    return source;
}

} // namespace flux::gfx
