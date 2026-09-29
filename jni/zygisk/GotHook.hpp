#pragma once

// A small GOT (import slot) patcher.
//
// Why not zygisk::Api::pltHookRegister: the public Zygisk API stops working after
// postAppSpecialize, but game engines load their libraries (libunity.so, libil2cpp.so, ...)
// afterwards. So the module patches what is loaded at specialization through this class and
// keeps patching newly loaded objects from its dlopen wrapper (event driven: no thread, no polling).
//
// Only imports are touched (JUMP_SLOT / GLOB_DAT relocations that name the symbol). Nothing is
// inline-patched, no code page is made writable, and a slot is restored to its exact original
// protection after writing.
//
// Not supported (reported through stats().unsupported_objects, never hidden): objects that carry
// packed relocations (DT_ANDROID_REL*, DT_RELR); their imports are simply not patched.

#include <cstddef>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct dl_phdr_info; // <link.h>

namespace flux::zygisk {

struct HookSpec {
    const char *symbol;
    void *replacement;
    void **original; ///< receives the pointer the slot held before patching (first one seen)
};

/// Decides per object and symbol whether a slot may be patched. Keeps system libraries out of it.
using Accept = bool (*)(const char *object_name, const char *symbol);

struct HookStats {
    size_t objects_scanned = 0;
    size_t slots_patched = 0;
    size_t unsupported_objects = 0; ///< packed relocations: not scanned
    size_t protect_failures = 0;
};

class GotHooker {
public:
    GotHooker(std::vector<HookSpec> specs, Accept accept) : specs_(std::move(specs)), accept_(accept) {}

    /// Patch every loaded object that has not been scanned yet. Returns slots patched by this call.
    /// Safe to call repeatedly (from a dlopen wrapper): already scanned objects are skipped.
    size_t scan_new_objects();

    /// Patch one specific object (tests, or a caller that already has the base).
    size_t patch_object(const char *name, uintptr_t base, const void *phdr, size_t phnum);

    HookStats stats() const;
    /// Did this call patch at least one slot for @p symbol (across all calls)?
    size_t patched_for(const std::string &symbol) const;

private:
    struct Ctx;
    static int iterate_cb(::dl_phdr_info *info, size_t size, void *data);

    std::vector<HookSpec> specs_;
    Accept accept_;
    mutable std::mutex mu_;
    std::set<uintptr_t> seen_;
    HookStats stats_;
    std::vector<std::pair<std::string, size_t>> per_symbol_;
};

/// Protection of the page holding @p addr according to /proc/self/maps (bit0 r, bit1 w, bit2 x); -1 if unknown.
int page_protection(uintptr_t addr);

} // namespace flux::zygisk
