#include "GotHook.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __LP64__
#define FLUX_R_SYM(i) ELF64_R_SYM(i)
#define FLUX_R_TYPE(i) ELF64_R_TYPE(i)
#else
#define FLUX_R_SYM(i) ELF32_R_SYM(i)
#define FLUX_R_TYPE(i) ELF32_R_TYPE(i)
#endif

// Relocation types that fill an import slot with a function address.
#if defined(__aarch64__)
#define FLUX_JUMP_SLOT R_AARCH64_JUMP_SLOT
#define FLUX_GLOB_DAT R_AARCH64_GLOB_DAT
#elif defined(__arm__)
#define FLUX_JUMP_SLOT R_ARM_JUMP_SLOT
#define FLUX_GLOB_DAT R_ARM_GLOB_DAT
#elif defined(__x86_64__)
#define FLUX_JUMP_SLOT R_X86_64_JUMP_SLOT
#define FLUX_GLOB_DAT R_X86_64_GLOB_DAT
#elif defined(__i386__)
#define FLUX_JUMP_SLOT R_386_JMP_SLOT
#define FLUX_GLOB_DAT R_386_GLOB_DAT
#else
#error "unsupported architecture for GotHooker"
#endif

// Packed-relocation tags (Android); defined here so the file builds against any elf.h.
#ifndef DT_ANDROID_REL
#define DT_ANDROID_REL 0x6000000f
#define DT_ANDROID_RELA 0x60000011
#endif
#ifndef DT_RELR
#define DT_RELR 36
#endif

namespace flux::zygisk {

namespace {

// Dynamic-section pointers are vaddrs on bionic and already-relocated addresses on glibc.
uintptr_t abs_addr(uintptr_t base, uintptr_t p) { return (p >= base && base != 0) ? p : base + p; }

struct DynInfo {
    const ElfW(Sym) *symtab = nullptr;
    const char *strtab = nullptr;
    uintptr_t jmprel = 0, rel = 0, rela = 0;
    size_t pltrelsz = 0, relsz = 0, relasz = 0;
    int pltrel_kind = 0; // DT_REL or DT_RELA
    bool packed = false;
};

bool read_dyn(uintptr_t base, const ElfW(Phdr) *ph, size_t n, DynInfo &out) {
    for (size_t i = 0; i < n; ++i) {
        if (ph[i].p_type != PT_DYNAMIC) continue;
        auto *dyn = reinterpret_cast<const ElfW(Dyn) *>(base + ph[i].p_vaddr);
        for (; dyn->d_tag != DT_NULL; ++dyn) {
            switch (dyn->d_tag) {
            case DT_SYMTAB: out.symtab = reinterpret_cast<const ElfW(Sym) *>(abs_addr(base, dyn->d_un.d_ptr)); break;
            case DT_STRTAB: out.strtab = reinterpret_cast<const char *>(abs_addr(base, dyn->d_un.d_ptr)); break;
            case DT_JMPREL: out.jmprel = abs_addr(base, dyn->d_un.d_ptr); break;
            case DT_PLTRELSZ: out.pltrelsz = dyn->d_un.d_val; break;
            case DT_PLTREL: out.pltrel_kind = static_cast<int>(dyn->d_un.d_val); break;
            case DT_REL: out.rel = abs_addr(base, dyn->d_un.d_ptr); break;
            case DT_RELSZ: out.relsz = dyn->d_un.d_val; break;
            case DT_RELA: out.rela = abs_addr(base, dyn->d_un.d_ptr); break;
            case DT_RELASZ: out.relasz = dyn->d_un.d_val; break;
            case DT_ANDROID_REL:
            case DT_ANDROID_RELA:
            case DT_RELR: out.packed = true; break;
            default: break;
            }
        }
        return out.symtab && out.strtab;
    }
    return false;
}

} // namespace

int page_protection(uintptr_t addr) {
    FILE *f = std::fopen("/proc/self/maps", "re");
    if (!f) return -1;
    char line[512];
    int prot = -1;
    while (std::fgets(line, sizeof line, f)) {
        unsigned long lo = 0, hi = 0;
        char perms[8] = {0};
        if (std::sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (addr >= lo && addr < hi) {
            prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) | (perms[2] == 'x' ? PROT_EXEC : 0);
            break;
        }
    }
    std::fclose(f);
    return prot;
}

namespace {

/// Write one pointer into an import slot, restoring the page's exact prior protection.
bool write_slot(uintptr_t slot, void *value, void **previous, size_t &protect_failures) {
    const long pg = sysconf(_SC_PAGESIZE);
    const uintptr_t page = slot & ~static_cast<uintptr_t>(pg - 1);
    int prot = page_protection(slot);
    if (prot < 0) {
        ++protect_failures;
        return false;
    }
    const bool need = !(prot & PROT_WRITE);
    if (need && mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pg), prot | PROT_WRITE) != 0) {
        ++protect_failures;
        return false;
    }
    auto *p = reinterpret_cast<void **>(slot);
    if (previous) *previous = *p;
    *p = value;
    if (need) mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pg), prot);
    return true;
}

} // namespace

size_t GotHooker::patch_object(const char *name, uintptr_t base, const void *phdr, size_t phnum) {
    DynInfo d;
    if (!read_dyn(base, static_cast<const ElfW(Phdr) *>(phdr), phnum, d)) return 0;
    if (d.packed) {
        std::lock_guard<std::mutex> g(mu_);
        ++stats_.unsupported_objects;
    }

    size_t patched = 0;
    auto handle = [&](uintptr_t r_offset, size_t sym_index, unsigned type) {
        if (type != FLUX_JUMP_SLOT && type != FLUX_GLOB_DAT) return;
        const char *sym = d.strtab + d.symtab[sym_index].st_name;
        for (auto &s : specs_) {
            if (std::strcmp(sym, s.symbol) != 0) continue;
            if (accept_ && !accept_(name, s.symbol)) continue;
            void *prev = nullptr;
            size_t pf = 0;
            if (write_slot(base + r_offset, s.replacement, &prev, pf)) {
                if (s.original && !*s.original && prev != s.replacement) *s.original = prev;
                ++patched;
                std::lock_guard<std::mutex> g(mu_);
                bool found = false;
                for (auto &e : per_symbol_)
                    if (e.first == s.symbol) { ++e.second; found = true; }
                if (!found) per_symbol_.emplace_back(s.symbol, 1);
            }
            std::lock_guard<std::mutex> g(mu_);
            stats_.protect_failures += pf;
        }
    };

    auto walk_rel = [&](uintptr_t table, size_t bytes) {
        auto *r = reinterpret_cast<const ElfW(Rel) *>(table);
        for (size_t i = 0; i < bytes / sizeof(ElfW(Rel)); ++i) handle(r[i].r_offset, FLUX_R_SYM(r[i].r_info), FLUX_R_TYPE(r[i].r_info));
    };
    auto walk_rela = [&](uintptr_t table, size_t bytes) {
        auto *r = reinterpret_cast<const ElfW(Rela) *>(table);
        for (size_t i = 0; i < bytes / sizeof(ElfW(Rela)); ++i) handle(r[i].r_offset, FLUX_R_SYM(r[i].r_info), FLUX_R_TYPE(r[i].r_info));
    };

    if (d.jmprel && d.pltrelsz) {
        if (d.pltrel_kind == DT_RELA) walk_rela(d.jmprel, d.pltrelsz);
        else walk_rel(d.jmprel, d.pltrelsz);
    }
    if (d.rel && d.relsz) walk_rel(d.rel, d.relsz);
    if (d.rela && d.relasz) walk_rela(d.rela, d.relasz);
    return patched;
}

struct GotHooker::Ctx {
    GotHooker *self;
    size_t patched = 0;
};

int GotHooker::iterate_cb(::dl_phdr_info *info, size_t, void *data) {
    auto *c = static_cast<Ctx *>(data);
    GotHooker *self = c->self;
    {
        std::lock_guard<std::mutex> g(self->mu_);
        if (!self->seen_.insert(info->dlpi_addr + reinterpret_cast<uintptr_t>(info->dlpi_phdr)).second) return 0;
        ++self->stats_.objects_scanned;
    }
    // The main executable has an empty name and no useful imports for us; skip it.
    if (!info->dlpi_name || !*info->dlpi_name) return 0;
    c->patched += self->patch_object(info->dlpi_name, info->dlpi_addr, info->dlpi_phdr, info->dlpi_phnum);
    return 0;
}

size_t GotHooker::scan_new_objects() {
    Ctx c{this, 0};
    dl_iterate_phdr(&GotHooker::iterate_cb, &c);
    std::lock_guard<std::mutex> g(mu_);
    stats_.slots_patched += c.patched;
    return c.patched;
}

HookStats GotHooker::stats() const {
    std::lock_guard<std::mutex> g(mu_);
    return stats_;
}

size_t GotHooker::patched_for(const std::string &symbol) const {
    std::lock_guard<std::mutex> g(mu_);
    for (auto &e : per_symbol_)
        if (e.first == symbol) return e.second;
    return 0;
}

} // namespace flux::zygisk
