// Reads identity the way a game engine does. Two Vulkan routes on purpose: the directly imported
// entry point (patched through the import slot) and vkGetInstanceProcAddr (wrapped by the provider).
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <jni.h>
#include <sys/system_properties.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string esc(const char *s) {
    std::string o;
    for (; s && *s; ++s) {
        if (*s == '"' || *s == '\\') o += '\\';
        if (static_cast<unsigned char>(*s) >= 0x20) o += *s;
    }
    return o;
}

std::string prop(const char *k) {
    char v[PROP_VALUE_MAX] = {0};
    __system_property_get(k, v);
    return v;
}

std::string vk_json(bool via_proc_addr) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) return "null";
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    if (!n) { vkDestroyInstance(inst, nullptr); return "null"; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst, &n, devs.data());
    VkPhysicalDeviceProperties p{};
    if (via_proc_addr) {
        auto fn = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceProperties"));
        if (fn) fn(devs[0], &p);
    } else {
        vkGetPhysicalDeviceProperties(devs[0], &p);
    }
    char buf[512];
    std::snprintf(buf, sizeof buf, "{\"deviceName\":\"%s\",\"vendorID\":%u,\"deviceID\":%u,\"apiVersion\":%u,\"driverVersion\":%u}",
                  esc(p.deviceName).c_str(), p.vendorID, p.deviceID, p.apiVersion, p.driverVersion);
    vkDestroyInstance(inst, nullptr);
    return buf;
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL Java_dev_flux_compattest_MainActivity_nativeProbe(JNIEnv *env, jobject, jstring jgl_vendor,
                                                                                        jstring jgl_renderer, jstring jgl_version) {
    (void)jgl_vendor; (void)jgl_renderer; (void)jgl_version;
    std::string j = "{";
    j += "\"props\":{";
    const char *keys[] = {"ro.product.model", "ro.product.brand", "ro.product.manufacturer", "ro.product.device",
                          "ro.product.name", "ro.build.fingerprint", "ro.soc.model", "ro.soc.manufacturer", "ro.hardware"};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; ++i) {
        if (i) j += ",";
        j += std::string("\"") + keys[i] + "\":\"" + esc(prop(keys[i]).c_str()) + "\"";
    }
    j += "},\"vk_direct\":" + vk_json(false) + ",\"vk_proc_addr\":" + vk_json(true);
    j += "}";
    return env->NewStringUTF(j.c_str());
}

// Called on the GL thread with a current context.
extern "C" JNIEXPORT jstring JNICALL Java_dev_flux_compattest_MainActivity_nativeGl(JNIEnv *env, jobject) {
    auto s = [](GLenum n) { const GLubyte *p = glGetString(n); return std::string(p ? reinterpret_cast<const char *>(p) : ""); };
    EGLDisplay d = eglGetCurrentDisplay();
    const char *ev = d != EGL_NO_DISPLAY ? eglQueryString(d, EGL_VENDOR) : "";
    std::string j = "{\"vendor\":\"" + esc(s(GL_VENDOR).c_str()) + "\",\"renderer\":\"" + esc(s(GL_RENDERER).c_str()) +
                    "\",\"version\":\"" + esc(s(GL_VERSION).c_str()) + "\",\"egl_vendor\":\"" + esc(ev) + "\"}";
    return env->NewStringUTF(j.c_str());
}
