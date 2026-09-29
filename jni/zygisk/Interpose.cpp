#include "Interpose.hpp"

#include <cstring>
#include <string>

namespace flux::zygisk::interpose {

namespace {

// Vulkan layouts that are fixed by the specification. Mirrored here (rather than including the
// Vulkan headers) because only the leading, ABI-stable members are needed.
struct VkPropsPrefix {
    uint32_t apiVersion, driverVersion, vendorID, deviceID, deviceType;
    char deviceName[256];
};
struct VkProps2Prefix {
    uint32_t sType;
    void *pNext;
    VkPropsPrefix properties;
};

constexpr unsigned kGlVendor = 0x1F00, kGlRenderer = 0x1F01, kGlVersion = 0x1F02;
constexpr int kEglVendor = 0x3053;

Items g_items;
ObservedFn g_observed;
Originals g_orig;
Counters g_counters;
GotHooker *g_hooker = nullptr;
thread_local bool t_in_scan = false;

// GL strings must stay valid for the life of the process: keep them in static storage.
std::string g_gl_vendor, g_gl_renderer, g_gl_version, g_egl_vendor;

void observed(const char *what, std::atomic<uint32_t> &counter) {
    if (counter.fetch_add(1, std::memory_order_relaxed) == 0 && g_observed) g_observed(what);
}

void patch_props(VkPropsPrefix &p) {
    const auto &vk = g_items.vk;
    if (!vk.device_name.empty()) {
        std::strncpy(p.deviceName, vk.device_name.c_str(), sizeof p.deviceName - 1);
        p.deviceName[sizeof p.deviceName - 1] = '\0';
    }
    if (vk.vendor_id) p.vendorID = *vk.vendor_id;
    if (vk.device_id) p.deviceID = *vk.device_id;
    if (vk.driver_version) p.driverVersion = *vk.driver_version;
    if (vk.api_version) {
        // Identity only: an API version can be reported lower than the real one, never higher.
        if (flux::compat::provider::vk_api_version_allowed(*vk.api_version, p.apiVersion)) p.apiVersion = *vk.api_version;
        else g_counters.vk_api_clamped.store(true, std::memory_order_relaxed);
    }
}

void after_scan() {
    if (!g_hooker || t_in_scan) return;
    t_in_scan = true;
    g_hooker->scan_new_objects();
    t_in_scan = false;
}

} // namespace

Originals &originals() { return g_orig; }
Counters &counters() { return g_counters; }
void set_hooker(GotHooker *h) { g_hooker = h; }

void set_items(const Items &items, ObservedFn on_observed) {
    g_items = items;
    g_observed = std::move(on_observed);
    auto get = [&](const char *k) -> std::string {
        auto it = items.gl.find(k);
        return it == items.gl.end() ? std::string() : it->second;
    };
    g_gl_vendor = get("vendor");
    g_gl_renderer = get("renderer");
    g_gl_version = get("version");
    g_egl_vendor = get("egl_vendor");
}

bool accept_object(const char *name, const char *symbol) {
    if (!name || !*name || !symbol) return false;
    const std::string n = name;
    // Objects that belong to the app: installed under /data (apk libs show up as ".../base.apk!/lib/...").
    // Our own module is a memfd/module-dir object, and system libraries are never patched: they
    // would see the spoof too (e.g. the GL loader reads ro.hardware to pick a driver).
    const bool app = n.rfind("/data/", 0) == 0 || n.rfind("/mnt/expand/", 0) == 0;
    if (n.find("flux_zygisk") != std::string::npos || n.find("/memfd:") != std::string::npos) return false;
    const std::string s = symbol;
    if (s == "__system_property_get") return app; // native property reads by the app only
    const size_t slash = n.rfind('/');
    const std::string base = slash == std::string::npos ? n : n.substr(slash + 1);
    if (s == "dlopen" || s == "android_dlopen_ext") return app || base == "libnativeloader.so";
    // GL / EGL / Vulkan: the app's own libraries, plus the framework's GLES/EGL JNI so that
    // android.opengl.GLES20.glGetString() from Java sees the same answer.
    return app || base == "libandroid_runtime.so";
}

std::vector<HookSpec> specs_for(const Items &items, bool watch_dlopen) {
    std::vector<HookSpec> v;
    auto &o = g_orig;
    auto add = [&](const char *sym, void *repl, void **orig) { v.push_back({sym, repl, orig}); };
    if (!items.props.empty())
        add("__system_property_get", reinterpret_cast<void *>(&flux_wrap_property_get), reinterpret_cast<void **>(&o.property_get));
    if (items.gl.count("vendor") || items.gl.count("renderer") || items.gl.count("version"))
        add("glGetString", reinterpret_cast<void *>(&flux_wrap_glGetString), reinterpret_cast<void **>(&o.glGetString));
    if (items.gl.count("egl_vendor"))
        add("eglQueryString", reinterpret_cast<void *>(&flux_wrap_eglQueryString), reinterpret_cast<void **>(&o.eglQueryString));
    if (items.vk.any()) {
        add("vkGetPhysicalDeviceProperties", reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties),
            reinterpret_cast<void **>(&o.vkGetPhysicalDeviceProperties));
        add("vkGetPhysicalDeviceProperties2", reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties2),
            reinterpret_cast<void **>(&o.vkGetPhysicalDeviceProperties2));
        add("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties2),
            reinterpret_cast<void **>(&o.vkGetPhysicalDeviceProperties2KHR));
        add("vkGetInstanceProcAddr", reinterpret_cast<void *>(&flux_wrap_vkGetInstanceProcAddr),
            reinterpret_cast<void **>(&o.vkGetInstanceProcAddr));
    }
    if (watch_dlopen) {
        add("dlopen", reinterpret_cast<void *>(&flux_wrap_dlopen), reinterpret_cast<void **>(&o.dlopen));
        add("android_dlopen_ext", reinterpret_cast<void *>(&flux_wrap_android_dlopen_ext), reinterpret_cast<void **>(&o.android_dlopen_ext));
    }
    return v;
}

extern "C" {

int flux_wrap_property_get(const char *name, char *value) {
    if (name && value) {
        auto it = g_items.props.find(name);
        if (it != g_items.props.end()) {
            observed("prop", g_counters.prop);
            // PROP_VALUE_MAX is 92 including the terminator.
            std::strncpy(value, it->second.c_str(), 91);
            value[91] = '\0';
            return static_cast<int>(std::strlen(value));
        }
    }
    return g_orig.property_get ? g_orig.property_get(name, value) : 0;
}

const unsigned char *flux_wrap_glGetString(unsigned int name) {
    const std::string *s = name == kGlVendor ? &g_gl_vendor : name == kGlRenderer ? &g_gl_renderer : name == kGlVersion ? &g_gl_version : nullptr;
    if (s && !s->empty()) {
        observed("gl", g_counters.gl);
        return reinterpret_cast<const unsigned char *>(s->c_str());
    }
    return g_orig.glGetString ? g_orig.glGetString(name) : nullptr;
}

const char *flux_wrap_eglQueryString(void *display, int name) {
    if (name == kEglVendor && !g_egl_vendor.empty()) {
        observed("egl", g_counters.egl);
        return g_egl_vendor.c_str();
    }
    return g_orig.eglQueryString ? g_orig.eglQueryString(display, name) : nullptr;
}

void flux_wrap_vkGetPhysicalDeviceProperties(void *pd, void *props) {
    if (g_orig.vkGetPhysicalDeviceProperties) g_orig.vkGetPhysicalDeviceProperties(pd, props);
    if (props && g_items.vk.any()) {
        patch_props(*static_cast<VkPropsPrefix *>(props));
        observed("vk", g_counters.vk);
    }
}

void flux_wrap_vkGetPhysicalDeviceProperties2(void *pd, void *props2) {
    auto real = g_orig.vkGetPhysicalDeviceProperties2 ? g_orig.vkGetPhysicalDeviceProperties2 : g_orig.vkGetPhysicalDeviceProperties2KHR;
    if (real) real(pd, props2);
    if (props2 && g_items.vk.any()) {
        patch_props(static_cast<VkProps2Prefix *>(props2)->properties);
        observed("vk", g_counters.vk);
    }
}

void *flux_wrap_vkGetInstanceProcAddr(void *instance, const char *name) {
    if (!name) return nullptr; // vkGetInstanceProcAddr(…, NULL) is invalid; never forward it
    void *fn = g_orig.vkGetInstanceProcAddr ? g_orig.vkGetInstanceProcAddr(instance, name) : nullptr;
    if (!fn) return fn;
    // Engines that resolve entry points dynamically never touch the GOT slots we patched.
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties")) {
        g_orig.vkGetPhysicalDeviceProperties = reinterpret_cast<void (*)(void *, void *)>(fn);
        return reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties);
    }
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties2")) {
        g_orig.vkGetPhysicalDeviceProperties2 = reinterpret_cast<void (*)(void *, void *)>(fn);
        return reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties2);
    }
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties2KHR")) {
        g_orig.vkGetPhysicalDeviceProperties2KHR = reinterpret_cast<void (*)(void *, void *)>(fn);
        return reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties2);
    }
    return fn;
}

void *flux_wrap_dlopen(const char *file, int flags) {
    void *h = g_orig.dlopen ? g_orig.dlopen(file, flags) : nullptr;
    if (h) after_scan();
    return h;
}

void *flux_wrap_android_dlopen_ext(const char *file, int flags, const void *extinfo) {
    void *h = g_orig.android_dlopen_ext ? g_orig.android_dlopen_ext(file, flags, extinfo) : nullptr;
    if (h) after_scan();
    return h;
}

} // extern "C"

} // namespace flux::zygisk::interpose
