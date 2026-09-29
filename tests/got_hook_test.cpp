// Host test for the GOT patcher (jni/zygisk/GotHook.*), run against real ELF fixtures.
// Same algorithm the provider runs inside a game process; nothing here needs Android.

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <cstring>

#include "flux_test.hpp"

#include "GotHook.hpp"

using namespace flux::zygisk;

namespace {

int (*real_import)(int) = nullptr;
int hooked_import(int x) { return real_import ? real_import(x) + 1 : -1; } // real result + 1: visible, and proves 'original' works

bool only_fixture_a(const char *name, const char *) { return name && std::strstr(name, "libgot_target_a") != nullptr; }
bool any_fixture(const char *name, const char *) { return name && std::strstr(name, "libgot_target_") != nullptr; }

typedef int (*call_t)(int);

call_t open_fixture(const char *path) {
    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { std::fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return nullptr; }
    return reinterpret_cast<call_t>(dlsym(h, "flux_call_import"));
}

void test_patches_only_accepted_objects_and_restores_protection() {
    void *prov = dlopen(FLUX_GOT_PROVIDER, RTLD_NOW | RTLD_GLOBAL);
    CHECK(prov != nullptr);
    call_t a = open_fixture(FLUX_GOT_TARGET_A);
    call_t b = open_fixture(FLUX_GOT_TARGET_B);
    CHECK(a && b);
    if (!a || !b) return;
    CHECK_EQ(a(5), 5 * 2 + 1000);
    CHECK_EQ(b(5), 5 * 2 + 1000);

    real_import = nullptr;
    GotHooker h({{"flux_import", reinterpret_cast<void *>(&hooked_import), reinterpret_cast<void **>(&real_import)}},
                &only_fixture_a);
    size_t n = h.scan_new_objects();
    CHECK(n >= 1);
    CHECK(h.patched_for("flux_import") >= 1);
    CHECK(real_import != nullptr);

    CHECK_EQ(a(5), 5 * 2 + 1 + 1000);   // hooked: real value + 1
    CHECK_EQ(b(5), 5 * 2 + 1000);       // not accepted: untouched
    CHECK(h.stats().protect_failures == 0);
    CHECK(h.stats().objects_scanned > 2); // it walked every loaded object...
    // ...but a second scan of the same objects patches nothing again
    CHECK(h.scan_new_objects() == 0);
}

void test_late_loaded_object_is_picked_up_by_a_later_scan() {
    // Uses fresh fixtures the first test did not load.
    real_import = nullptr;
    GotHooker h({{"flux_import", reinterpret_cast<void *>(&hooked_import), reinterpret_cast<void **>(&real_import)}},
                &any_fixture);
    h.scan_new_objects();                       // c is not loaded yet
    call_t c = open_fixture(FLUX_GOT_TARGET_C);
    CHECK(c != nullptr);
    if (!c) return;
    CHECK_EQ(c(3), 3 * 2 + 1000);               // loaded after the scan: still real
    CHECK(h.scan_new_objects() >= 1);           // the dlopen wrapper's job
    CHECK_EQ(c(3), 3 * 2 + 1 + 1000);
}

void test_unknown_symbol_and_no_accept_patch_nothing() {
    real_import = nullptr;
    GotHooker none({{"no_such_symbol_anywhere", reinterpret_cast<void *>(&hooked_import), nullptr}}, &any_fixture);
    CHECK(none.scan_new_objects() == 0);

    GotHooker rejecting({{"flux_import", reinterpret_cast<void *>(&hooked_import), nullptr}},
                        [](const char *, const char *) { return false; });
    CHECK(rejecting.scan_new_objects() == 0);
}

void test_page_protection_reads_maps() {
    int local = 0;
    int p = page_protection(reinterpret_cast<uintptr_t>(&local));
    CHECK(p >= 0);
    CHECK((p & PROT_READ) != 0);
    CHECK(page_protection(0) == -1);
}

} // namespace

int main() {
    test_patches_only_accepted_objects_and_restores_protection();
    test_late_loaded_object_is_picked_up_by_a_later_scan();
    test_unknown_symbol_and_no_accept_patch_nothing();
    test_page_protection_reads_maps();
    return flux_test::report("got_hook_test");
}
