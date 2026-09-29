// Host tests for the provider's in-process wrappers (jni/zygisk/Interpose.*).
// The real GL / Vulkan / property functions are replaced by fakes; the wrappers are driven directly.

#include <cstring>
#include <string>
#include <vector>

#include "flux_test.hpp"

#include "Interpose.hpp"

using namespace flux::zygisk;
using namespace flux::zygisk::interpose;
using flux::compat::provider::Items;

namespace {

// ---- fakes standing in for the real driver ---------------------------------------------------
int fake_property_get(const char *name, char *value) {
    std::strcpy(value, std::string(name) == "ro.product.model" ? "RealModel" : "real-other");
    return static_cast<int>(std::strlen(value));
}
const unsigned char *fake_gl(unsigned int n) {
    static const char *v = "RealVendor", *r = "RealRenderer", *ver = "OpenGL ES 3.2 real", *ext = "GL_REAL_EXT";
    return reinterpret_cast<const unsigned char *>(n == 0x1F00 ? v : n == 0x1F01 ? r : n == 0x1F02 ? ver : ext);
}
const char *fake_egl(void *, int n) { return n == 0x3053 ? "RealEGL" : "RealEGLOther"; }

struct Props {
    uint32_t apiVersion, driverVersion, vendorID, deviceID, deviceType;
    char deviceName[256];
    unsigned char rest[64]; // stands in for limits / sparse properties: must never be touched
};
struct Props2 {
    uint32_t sType;
    void *pNext;
    Props properties;
};
constexpr uint32_t make_api(unsigned a, unsigned b, unsigned c) { return (a << 22) | (b << 12) | c; }

void fake_vk_props(void *, void *out) {
    if (!out) return; // the real driver would reject this too; the wrapper must merely not add a crash
    auto *p = static_cast<Props *>(out);
    p->apiVersion = make_api(1, 1, 0);
    p->driverVersion = 111;
    p->vendorID = 0x5143;
    p->deviceID = 0x6050001;
    p->deviceType = 1;
    std::strcpy(p->deviceName, "Real GPU");
    std::memset(p->rest, 0xAB, sizeof p->rest);
}
void fake_vk_props2(void *d, void *out) {
    if (out) fake_vk_props(d, &static_cast<Props2 *>(out)->properties);
}
void *fake_gipa(void *, const char *name) {
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties")) return reinterpret_cast<void *>(&fake_vk_props);
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties2")) return reinterpret_cast<void *>(&fake_vk_props2);
    if (!std::strcmp(name, "vkCreateDevice")) return reinterpret_cast<void *>(0x1234);
    return nullptr;
}

std::vector<std::string> seen;
ObservedFn observer() { return [](const char *w) { seen.push_back(w); }; }

void wire_fakes() {
    auto &o = originals();
    o = Originals{};
    o.property_get = &fake_property_get;
    o.glGetString = &fake_gl;
    o.eglQueryString = &fake_egl;
    o.vkGetPhysicalDeviceProperties = &fake_vk_props;
    o.vkGetPhysicalDeviceProperties2 = &fake_vk_props2;
    o.vkGetInstanceProcAddr = &fake_gipa;
}

std::string str(const unsigned char *p) { return p ? reinterpret_cast<const char *>(p) : "(null)"; }

// ----------------------------------------------------------------------------------------------------------

void test_property_reads() {
    Items it;
    it.props["ro.product.model"] = "FlagX";
    seen.clear();
    wire_fakes();
    set_items(it, observer());
    char buf[92];
    CHECK(flux_wrap_property_get("ro.product.model", buf) == 5);
    CHECK_EQ(std::string(buf), "FlagX");
    flux_wrap_property_get("ro.product.model", buf);
    CHECK(seen.size() == 1 && seen[0] == "prop");                 // reported once, not per call
    flux_wrap_property_get("ro.product.brand", buf);               // not in the plan: real answer
    CHECK_EQ(std::string(buf), "real-other");

    Items longv;
    longv.props["ro.x"] = std::string(200, 'a');
    set_items(longv, observer());
    flux_wrap_property_get("ro.x", buf);
    CHECK(std::strlen(buf) == 91);                                 // never overruns PROP_VALUE_MAX
}

void test_gl_and_egl_strings() {
    Items it;
    it.gl["renderer"] = "Acme GPU 9";
    it.gl["egl_vendor"] = "Acme";
    seen.clear();
    wire_fakes();
    set_items(it, observer());
    CHECK_EQ(str(flux_wrap_glGetString(0x1F01)), "Acme GPU 9");
    CHECK_EQ(str(flux_wrap_glGetString(0x1F00)), "RealVendor");    // vendor not requested: real
    CHECK_EQ(str(flux_wrap_glGetString(0x1F03)), "GL_REAL_EXT");   // extensions are never touched
    CHECK_EQ(std::string(flux_wrap_eglQueryString(nullptr, 0x3053)), "Acme");
    CHECK_EQ(std::string(flux_wrap_eglQueryString(nullptr, 0x3054)), "RealEGLOther");
    // Returned pointers stay valid: the same call returns the same stable string.
    CHECK(flux_wrap_glGetString(0x1F01) == flux_wrap_glGetString(0x1F01));
    CHECK(seen.size() == 2);
}

void test_gl_without_driver_does_not_crash() {
    Items it;
    it.gl["renderer"] = "X";
    wire_fakes();
    originals().glGetString = nullptr;
    set_items(it, observer());
    CHECK(flux_wrap_glGetString(0x1F00) == nullptr);   // unrequested + no driver: null, not a crash
    CHECK_EQ(str(flux_wrap_glGetString(0x1F01)), "X");
}

void test_vulkan_identity_only() {
    Items it;
    it.vk.device_name = "Acme GPU 9";
    it.vk.vendor_id = 0x1234;
    it.vk.device_id = 77;
    it.vk.driver_version = 999;
    it.vk.api_version = make_api(1, 0, 0); // lower than the real 1.1
    seen.clear();
    wire_fakes();
    set_items(it, observer());
    counters().vk_api_clamped = false;

    Props p{};
    flux_wrap_vkGetPhysicalDeviceProperties(nullptr, &p);
    CHECK_EQ(std::string(p.deviceName), "Acme GPU 9");
    CHECK(p.vendorID == 0x1234 && p.deviceID == 77 && p.driverVersion == 999);
    CHECK(p.apiVersion == make_api(1, 0, 0));
    CHECK(p.deviceType == 1);                                  // untouched
    for (unsigned char b : p.rest) CHECK(b == 0xAB);           // limits / sparse props untouched

    Props2 p2{};
    p2.sType = 1000059001;
    flux_wrap_vkGetPhysicalDeviceProperties2(nullptr, &p2);
    CHECK_EQ(std::string(p2.properties.deviceName), "Acme GPU 9");
    CHECK(p2.sType == 1000059001);
    CHECK(seen.size() == 1 && seen[0] == "vk");
    CHECK(!counters().vk_api_clamped.load());
}

void test_vulkan_api_version_is_never_raised() {
    Items it;
    it.vk.api_version = make_api(1, 3, 0); // the plan asks for MORE than the GPU has (1.1)
    it.vk.device_name = "Acme";
    wire_fakes();
    set_items(it, observer());
    counters().vk_api_clamped = false;
    Props p{};
    flux_wrap_vkGetPhysicalDeviceProperties(nullptr, &p);
    CHECK(p.apiVersion == make_api(1, 1, 0));                  // real value kept
    CHECK(counters().vk_api_clamped.load());                   // and it is reported, not hidden
    CHECK_EQ(std::string(p.deviceName), "Acme");               // the rest of the identity still applies
}

void test_vulkan_partial_fields() {
    Items it;
    it.vk.device_name = "OnlyName";
    wire_fakes();
    set_items(it, observer());
    Props p{};
    flux_wrap_vkGetPhysicalDeviceProperties(nullptr, &p);
    CHECK_EQ(std::string(p.deviceName), "OnlyName");
    CHECK(p.vendorID == 0x5143 && p.deviceID == 0x6050001);    // unrequested fields stay real
}

void test_dynamic_proc_addr_returns_wrappers_only_for_properties() {
    Items it;
    it.vk.device_name = "X";
    wire_fakes();
    originals().vkGetPhysicalDeviceProperties = nullptr;
    originals().vkGetPhysicalDeviceProperties2 = nullptr;
    set_items(it, observer());
    CHECK(flux_wrap_vkGetInstanceProcAddr(nullptr, "vkGetPhysicalDeviceProperties") ==
          reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties));
    CHECK(flux_wrap_vkGetInstanceProcAddr(nullptr, "vkGetPhysicalDeviceProperties2") ==
          reinterpret_cast<void *>(&flux_wrap_vkGetPhysicalDeviceProperties2));
    CHECK(flux_wrap_vkGetInstanceProcAddr(nullptr, "vkCreateDevice") == reinterpret_cast<void *>(0x1234)); // untouched
    CHECK(flux_wrap_vkGetInstanceProcAddr(nullptr, "vkNope") == nullptr);
    // The wrapper learned the real function from the lookup and calls through it.
    Props p{};
    flux_wrap_vkGetPhysicalDeviceProperties(nullptr, &p);
    CHECK_EQ(std::string(p.deviceName), "X");
    CHECK(p.vendorID == 0x5143);
}

void test_null_arguments_are_safe() {
    Items it;
    it.vk.device_name = "X";
    it.props["a"] = "b";
    wire_fakes();
    set_items(it, observer());
    flux_wrap_vkGetPhysicalDeviceProperties(nullptr, nullptr);
    flux_wrap_vkGetPhysicalDeviceProperties2(nullptr, nullptr);
    CHECK(flux_wrap_vkGetInstanceProcAddr(nullptr, nullptr) == nullptr);
    originals().property_get = nullptr;
    char buf[92] = {0};
    CHECK(flux_wrap_property_get(nullptr, buf) == 0);
}

// ---- which objects may be patched --------------------------------------------------------------------------------------

void test_object_acceptance_policy() {
    const char *app_lib = "/data/app/~~x/com.example.game-y/lib/arm64/libunity.so";
    const char *apk_lib = "/data/app/~~x/com.example.game-y/base.apk!/lib/arm64-v8a/libil2cpp.so";
    // App code: everything the plan may need.
    for (const char *sym : {"__system_property_get", "glGetString", "eglQueryString", "vkGetPhysicalDeviceProperties", "dlopen"}) {
        CHECK(accept_object(app_lib, sym));
        CHECK(accept_object(apk_lib, sym));
    }
    // System libraries never see property spoofing (the GL loader reads ro.hardware to pick a driver).
    for (const char *sys : {"/system/lib64/libc.so", "/apex/com.android.runtime/lib64/bionic/libc.so", "/vendor/lib64/egl/libGLES_mali.so",
                            "/system/lib64/libhwui.so", "/system/lib64/libandroid_runtime.so", "/system/lib64/libcutils.so"})
        CHECK(!accept_object(sys, "__system_property_get"));
    // Java GLES/EGL JNI lives in libandroid_runtime: it may answer GL queries so Java and native agree...
    CHECK(accept_object("/system/lib64/libandroid_runtime.so", "glGetString"));
    CHECK(accept_object("/system/lib64/libandroid_runtime.so", "eglQueryString"));
    // ...but other system libraries are left alone.
    CHECK(!accept_object("/system/lib64/libhwui.so", "glGetString"));
    CHECK(!accept_object("/vendor/lib64/egl/libGLESv2_adreno.so", "glGetString"));
    // The Java library loader is the one system object whose dlopen import is watched.
    CHECK(accept_object("/system/lib64/libnativeloader.so", "android_dlopen_ext"));
    CHECK(!accept_object("/system/lib64/libnativeloader.so", "glGetString"));
    // Never ourselves.
    CHECK(!accept_object("/data/adb/modules/flux/zygisk/flux_zygisk.so", "glGetString"));
    CHECK(!accept_object("/memfd:jit-cache", "glGetString"));
    CHECK(!accept_object("", "glGetString"));
    CHECK(!accept_object(nullptr, "glGetString"));
}

void test_specs_are_limited_to_what_the_plan_needs() {
    Items dev;
    dev.props["ro.product.model"] = "X";
    dev.build["MODEL"] = "X";
    auto s = specs_for(dev, false);
    CHECK(s.size() == 1 && std::string(s[0].symbol) == "__system_property_get");

    Items gpu;
    gpu.gl["renderer"] = "R";
    auto g = specs_for(gpu, true);
    std::vector<std::string> names;
    for (auto &x : g) names.push_back(x.symbol);
    CHECK((names == std::vector<std::string>{"glGetString", "dlopen", "android_dlopen_ext"}));

    Items none;
    CHECK(specs_for(none, false).empty());              // nothing requested => no hook at all

    Items vk;
    vk.vk.device_name = "V";
    CHECK(specs_for(vk, false).size() == 4);
    CHECK(specs_for(vk, false).size() == 4);
}

void test_dlopen_wrappers_forward_and_survive_missing_hooker() {
    wire_fakes();
    originals().dlopen = [](const char *, int) -> void * { return reinterpret_cast<void *>(0x42); };
    originals().android_dlopen_ext = [](const char *, int, const void *) -> void * { return nullptr; };
    set_hooker(nullptr);
    CHECK(flux_wrap_dlopen("libx.so", 0) == reinterpret_cast<void *>(0x42));
    CHECK(flux_wrap_android_dlopen_ext("libx.so", 0, nullptr) == nullptr);   // failure is passed through
}

} // namespace

int main() {
    test_property_reads();
    test_gl_and_egl_strings();
    test_gl_without_driver_does_not_crash();
    test_vulkan_identity_only();
    test_vulkan_api_version_is_never_raised();
    test_vulkan_partial_fields();
    test_dynamic_proc_addr_returns_wrappers_only_for_properties();
    test_null_arguments_are_safe();
    test_object_acceptance_policy();
    test_specs_are_limited_to_what_the_plan_needs();
    test_dlopen_wrappers_forward_and_survive_missing_hooker();
    return flux_test::report("interpose_test");
}
