#include "GraphicsIntelligence.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>

namespace flux::graphics {

namespace {

namespace cx = flux::context;
using Facts = std::vector<cx::CapabilityFact>;

cx::CapabilityFact fact(const std::string &id, cx::Support support, cx::Confidence confidence,
                        const std::string &value, const std::string &source, const std::string &note,
                        const std::string &interface = "") {
    cx::CapabilityFact f;
    f.id = id;
    f.domain = kDomain;
    f.source = source;
    f.support = support;
    f.readable = support == cx::Support::Yes && !value.empty();
    f.writable = false; // capability facts are never write targets
    f.verified = false;
    f.confidence = confidence;
    f.risk = cx::Risk::Low;
    f.interface = interface;
    f.value = value;
    f.note = note;
    return f;
}

cx::CapabilityFact unknown(const std::string &id, const std::string &why) {
    return fact(id, cx::Support::Unknown, cx::Confidence::None, "", "", why);
}

std::string prop(const GraphicsEvidence &e, const char *key) {
    if (!e.property) return "";
    auto v = e.property(key);
    auto end = v.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? "" : v.substr(0, end + 1);
}

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::optional<std::string> read_line(const flux::kernel::ReadOnlyFs *fs, const std::string &path) {
    if (!fs) return std::nullopt;
    auto raw = fs->read(path);
    if (!raw) return std::nullopt;
    auto t = *raw;
    t.erase(t.find_last_not_of(" \t\r\n") + 1);
    if (t.empty() || t.size() > 256 || t.find('\n') != std::string::npos) return std::nullopt;
    return t;
}

bool exists(const flux::kernel::ReadOnlyFs *fs, const std::string &path) {
    return fs && fs->kind(path) != flux::kernel::ReadOnlyFs::Kind::Missing;
}

/// One source's view of the GPU.
struct Claim {
    std::string source;
    std::string vendor;
    std::string model;
    cx::Confidence confidence;
    std::string interface;
};

/// Vendor named by an EGL/Vulkan HAL suffix; "" when the name is not a known GPU family.
std::string vendor_from_hal(const std::string &name) {
    const auto n = lower(name);
    if (n == "adreno") return "qualcomm";
    if (n == "mali") return "arm";
    if (n == "powervr") return "imagination";
    return "";
}

std::vector<Claim> gpu_claims(const GraphicsEvidence &e) {
    std::vector<Claim> out;
    if (e.vulkan && e.vulkan->available) {
        auto v = flux::gfx::vendor_name_for_id(e.vulkan->vendor_id);
        if (v != "unknown") out.push_back({"vulkan", v, e.vulkan->device_name, cx::Confidence::High, "vkGetPhysicalDeviceProperties"});
    }
    if (exists(e.fs, "sys/class/kgsl/kgsl-3d0")) {
        auto model = read_line(e.fs, "sys/class/kgsl/kgsl-3d0/gpu_model");
        const bool named = model && lower(*model).rfind("adreno", 0) == 0;
        out.push_back({"sysfs", "qualcomm", model.value_or(""), named ? cx::Confidence::High : cx::Confidence::Medium,
                       "sys/class/kgsl/kgsl-3d0"});
    } else if (exists(e.fs, "sys/class/misc/mali0")) {
        auto model = read_line(e.fs, "sys/class/misc/mali0/device/gpuinfo");
        out.push_back({"sysfs", "arm", model.value_or(""), cx::Confidence::Medium, "sys/class/misc/mali0"});
    }
    if (auto egl = prop(e, "ro.hardware.egl"); !vendor_from_hal(egl).empty())
        out.push_back({"egl", vendor_from_hal(egl), "", cx::Confidence::Low, "ro.hardware.egl"});
    return out;
}

void add_gpu(const GraphicsEvidence &e, Facts &out) {
    const auto claims = gpu_claims(e);
    for (const auto &c : claims) {
        out.push_back(fact("graphics.gpu.vendor." + c.source, cx::Support::Yes, c.confidence, c.vendor, c.source,
                           "per-source evidence", c.interface));
        if (!c.model.empty())
            out.push_back(fact("graphics.gpu.model." + c.source, cx::Support::Yes, c.confidence, c.model, c.source,
                               "per-source evidence", c.interface));
    }
    if (claims.empty()) {
        out.push_back(unknown("graphics.gpu.vendor", "no GPU evidence (no Vulkan probe, GPU sysfs or EGL HAL name)"));
        out.push_back(unknown("graphics.gpu.model", "no GPU evidence"));
        return;
    }
    auto top = std::max_element(claims.begin(), claims.end(),
                                [](const Claim &a, const Claim &b) { return a.confidence < b.confidence; })->confidence;
    std::string summary;
    bool tie_conflict = false, any_conflict = false;
    const Claim *best = nullptr;
    for (const auto &c : claims) {
        summary += (summary.empty() ? "" : " ") + c.source + "=" + c.vendor;
        if (c.confidence == top && !best) best = &c;
    }
    for (const auto &c : claims) {
        if (c.vendor == best->vendor) continue;
        any_conflict = true;
        if (c.confidence == top) tie_conflict = true;
    }
    if (tie_conflict) {
        out.push_back(fact("graphics.gpu.vendor", cx::Support::Unknown, cx::Confidence::None, "", "",
                           "conflict between equally confident sources: " + summary));
        out.push_back(unknown("graphics.gpu.model", "GPU vendor conflict: " + summary));
        return;
    }
    out.push_back(fact("graphics.gpu.vendor", cx::Support::Yes, top, best->vendor, best->source,
                       any_conflict ? "conflict (lower-confidence sources disagree): " + summary : "sources: " + summary,
                       best->interface));
    // Model: first source (priority vulkan > sysfs) that agrees on the vendor and names a model.
    for (const auto &c : claims) {
        if (c.vendor != best->vendor || c.model.empty()) continue;
        out.push_back(fact("graphics.gpu.model", cx::Support::Yes, c.confidence, c.model, c.source, "", c.interface));
        return;
    }
    out.push_back(unknown("graphics.gpu.model", "vendor known, no source reports a model"));
}

bool vulkan_driver_file(const GraphicsEvidence &e, const std::string &hal) {
    if (!e.fs) return false;
    for (const char *dir : {"vendor/lib64/hw", "vendor/lib/hw"}) {
        if (!hal.empty() && exists(e.fs, std::string(dir) + "/vulkan." + hal + ".so")) return true;
        for (const auto &name : e.fs->list(dir))
            if (name.rfind("vulkan.", 0) == 0 && name.size() > 10 && name.substr(name.size() - 3) == ".so") return true;
    }
    return false;
}

cx::Support add_vulkan(const GraphicsEvidence &e, Facts &out) {
    if (e.vulkan) {
        const auto &v = *e.vulkan;
        if (v.available) {
            out.push_back(fact("graphics.vulkan.available", cx::Support::Yes, cx::Confidence::High, "yes", "vulkan",
                               "instance created, " + std::to_string(v.device_count) + " device(s)"));
            out.push_back(fact("graphics.vulkan.api_version", cx::Support::Yes, cx::Confidence::High,
                               flux::gfx::format_vulkan_version(v.api_version_raw), "vulkan", ""));
            char raw[16];
            std::snprintf(raw, sizeof raw, "0x%08x", v.driver_version_raw);
            out.push_back(fact("graphics.driver.vulkan_version", cx::Support::Yes, cx::Confidence::High, raw, "vulkan",
                               "raw VkPhysicalDeviceProperties::driverVersion (encoding is vendor-specific)"));
            return cx::Support::Yes;
        }
        out.push_back(fact("graphics.vulkan.available", cx::Support::No, cx::Confidence::High, "", "vulkan",
                           "status=" + v.status + (v.detail.empty() ? "" : " " + v.detail) +
                               (v.loader_present ? " (loader present)" : " (no loader)")));
        out.push_back(unknown("graphics.vulkan.api_version", "Vulkan unavailable"));
        out.push_back(unknown("graphics.driver.vulkan_version", "Vulkan unavailable"));
        return cx::Support::No;
    }
    const auto hal = prop(e, "ro.hardware.vulkan");
    const bool file = vulkan_driver_file(e, hal);
    if (file || !hal.empty()) {
        out.push_back(fact("graphics.vulkan.available", cx::Support::Yes, file ? cx::Confidence::Medium : cx::Confidence::Low,
                           "yes", file ? "driver_file" : "property",
                           file ? "vendor Vulkan driver present; no instance created" : "ro.hardware.vulkan set; driver file not seen"));
    } else {
        out.push_back(unknown("graphics.vulkan.available", "no instance probe and no declarative evidence"));
    }
    out.push_back(unknown("graphics.vulkan.api_version", "needs an instance probe"));
    out.push_back(unknown("graphics.driver.vulkan_version", "needs an instance probe"));
    return file || !hal.empty() ? cx::Support::Yes : cx::Support::Unknown;
}

/// GPU interfaces come from the kernel's facts; graphics does not re-probe sysfs nodes.
void add_kernel_interface(const GraphicsEvidence &e, Facts &out, const std::string &id, const std::regex &match,
                          const char *what) {
    if (!e.context) {
        out.push_back(unknown(id, "no kernel capability facts available"));
        return;
    }
    std::vector<std::string> gpu_ids = e.context->ids("gpu"), hits;
    for (const auto &kid : gpu_ids) {
        auto r = e.context->resolve(kid);
        if (r.support == cx::Support::Yes && r.fact && r.fact->readable && std::regex_search(kid, match))
            hits.push_back(kid);
    }
    if (gpu_ids.empty()) {
        out.push_back(unknown(id, "kernel published no GPU facts"));
    } else if (hits.empty()) {
        out.push_back(fact(id, cx::Support::No, cx::Confidence::Medium, "", "kernel",
                           std::string("no readable GPU ") + what + " interface among kernel facts"));
    } else {
        std::string v;
        for (const auto &h : hits) v += (v.empty() ? "" : " ") + h;
        out.push_back(fact(id, cx::Support::Yes, cx::Confidence::High, v, "kernel", "kernel capability ids"));
    }
}

} // namespace

std::string format_gles_version(const std::string &value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); }) ||
        value.size() > 9)
        return "";
    const long v = std::stol(value);
    const long major = v >> 16, minor = v & 0xffff;
    if (major < 1 || major > 9 || minor > 9) return "";
    return std::to_string(major) + "." + std::to_string(minor);
}

std::vector<cx::CapabilityFact> observe(const GraphicsEvidence &e) {
    Facts out;
    add_gpu(e, out);
    const auto vk = add_vulkan(e, out);

    const auto gles = format_gles_version(prop(e, "ro.opengles.version"));
    out.push_back(gles.empty() ? unknown("graphics.opengles.version", "ro.opengles.version unset or invalid")
                               : fact("graphics.opengles.version", cx::Support::Yes, cx::Confidence::Medium, gles,
                                      "property", "declared by the vendor (ro.opengles.version)", "ro.opengles.version"));
    const auto egl = prop(e, "ro.hardware.egl");
    out.push_back(egl.empty() ? unknown("graphics.egl.driver", "ro.hardware.egl unset")
                              : fact("graphics.egl.driver", cx::Support::Yes, cx::Confidence::Medium, egl, "property",
                                     "EGL/GLES HAL name", "ro.hardware.egl"));
    const auto updatable = prop(e, "ro.gfx.driver.0");
    out.push_back(!e.property ? unknown("graphics.driver.updatable", "no property reader")
                  : updatable.empty()
                      ? fact("graphics.driver.updatable", cx::Support::No, cx::Confidence::Low, "", "property", "ro.gfx.driver.0 unset")
                      : fact("graphics.driver.updatable", cx::Support::Yes, cx::Confidence::Medium, updatable, "property",
                             "updatable graphics driver package", "ro.gfx.driver.0"));

    std::string ifs;
    if (vk == cx::Support::Yes) ifs += "vulkan";
    if (!gles.empty()) ifs += std::string(ifs.empty() ? "" : " ") + "opengles";
    if (!egl.empty()) ifs += std::string(ifs.empty() ? "" : " ") + "egl";
    out.push_back(ifs.empty() ? unknown("graphics.interfaces", "no graphics API evidence")
                              : fact("graphics.interfaces", cx::Support::Yes, cx::Confidence::Medium, ifs, "derived",
                                     "APIs with Yes above; confidence of each is on its own fact"));

    static const std::regex freq(R"(freq|clk|opp)"), load(R"(busy|util|load)");
    add_kernel_interface(e, out, "graphics.gpu.freq_interface", freq, "frequency");
    add_kernel_interface(e, out, "graphics.gpu.load_interface", load, "load");
    return out;
}

void publish(const GraphicsEvidence &evidence, cx::CapabilityContext &context) {
    auto facts = observe(evidence); // reads the context before replacing the graphics snapshot
    context.publish(kPublisher, std::move(facts));
}

} // namespace flux::graphics
