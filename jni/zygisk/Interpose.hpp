#pragma once

// Wrappers installed into a target game process. They only ever answer with what the plan
// carries and otherwise call straight through to the real function.
//
//   __system_property_get   native reads of the plan's ro.* identity properties
//   glGetString             GL_VENDOR / GL_RENDERER / GL_VERSION
//   eglQueryString          EGL_VENDOR
//   vkGetPhysicalDeviceProperties(2/2KHR)  deviceName, vendorID, deviceID, driverVersion, apiVersion
//   vkGetInstanceProcAddr   so engines that resolve Vulkan entry points dynamically get the wrappers
//   dlopen / android_dlopen_ext  only to scan libraries loaded later (event driven, no thread)
//
// Nothing here fabricates hardware capability: Vulkan features, extensions, limits and queue
// families are never touched, and a Vulkan apiVersion can be lowered but never raised.

#include <atomic>
#include <cstdint>
#include <functional>

#include "GotHook.hpp"
#include "ProviderPlan.hpp"

namespace flux::zygisk::interpose {

using flux::compat::provider::Items;

/// Called once per mechanism the first time it actually answers a query ("gl", "egl", "vk", "prop").
using ObservedFn = std::function<void(const char *what)>;

/// Install the plan's items. Must be called before any wrapper runs; the items are immutable afterwards.
void set_items(const Items &items, ObservedFn on_observed);

/// Symbols to patch for the current items (only those the plan needs), plus the dlopen watchers.
std::vector<HookSpec> specs_for(const Items &items, bool watch_dlopen);
bool accept_object(const char *object_name, const char *symbol);

// Wrappers, exposed so the host tests can drive them without a GOT.
extern "C" {
int flux_wrap_property_get(const char *name, char *value);
const unsigned char *flux_wrap_glGetString(unsigned int name);
const char *flux_wrap_eglQueryString(void *display, int name);
void flux_wrap_vkGetPhysicalDeviceProperties(void *physical_device, void *props);
void flux_wrap_vkGetPhysicalDeviceProperties2(void *physical_device, void *props2);
void *flux_wrap_vkGetInstanceProcAddr(void *instance, const char *name);
void *flux_wrap_dlopen(const char *file, int flags);
void *flux_wrap_android_dlopen_ext(const char *file, int flags, const void *extinfo);
}

/// Real function pointers, filled by the GOT patcher (tests set them to fakes).
struct Originals {
    int (*property_get)(const char *, char *) = nullptr;
    const unsigned char *(*glGetString)(unsigned int) = nullptr;
    const char *(*eglQueryString)(void *, int) = nullptr;
    void (*vkGetPhysicalDeviceProperties)(void *, void *) = nullptr;
    void (*vkGetPhysicalDeviceProperties2)(void *, void *) = nullptr;
    void (*vkGetPhysicalDeviceProperties2KHR)(void *, void *) = nullptr;
    void *(*vkGetInstanceProcAddr)(void *, const char *) = nullptr;
    void *(*dlopen)(const char *, int) = nullptr;
    void *(*android_dlopen_ext)(const char *, int, const void *) = nullptr;
};
Originals &originals();

/// Set by the module once the hooker exists so the dlopen wrappers can scan new objects.
void set_hooker(GotHooker *hooker);

struct Counters {
    std::atomic<uint32_t> prop{0}, gl{0}, egl{0}, vk{0};
    std::atomic<bool> vk_api_clamped{false};
};
Counters &counters();

} // namespace flux::zygisk::interpose
