// Runtime capability bootstrap (Step 7.6).
//
// fluxd start -> Kernel Intelligence probe -> CapabilityContext publish -> engines query.
//
// Ownership: Kernel Intelligence collects kernel facts; CapabilityContext stores the normalised
// runtime state; SynthesisCore/Aeyrin owns future schema adaptation (schema v4 untouched, D-11).
// The bootstrap only moves facts from one to the other. It never writes a node and never decides.
#pragma once

#include "CapabilityContext.hpp"
#include "KernelIntelligence.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace flux::kernel {

enum class BootstrapStatus {
    NotRun,
    Ok,     // published; at least one capability supported
    Empty,  // published; no capability supported (everything No/Unknown)
    Failed, // probe threw; nothing new published
};
const char *to_string(BootstrapStatus s);

struct BootstrapResult {
    BootstrapStatus status = BootstrapStatus::NotRun;
    int attempt = 0;          // 1 for the first run
    size_t facts = 0;         // facts published by this run (0 on failure)
    size_t supported = 0;     // of those, Support::Yes
    uint64_t generation = 0;  // context generation after this run
    std::string error;        // probe failure text
};

/// Future Observatory hook (no event type, no storage yet). Exceptions are swallowed.
using BootstrapObserver = std::function<void(const BootstrapResult &)>;

class CapabilityBootstrap {
  public:
    using Probe = std::function<KernelReport()>;

    CapabilityBootstrap(std::shared_ptr<flux::context::CapabilityContext> context, Probe probe);

    /// Probe and publish. Never throws. On failure the context keeps whatever the kernel
    /// publisher had before (nothing on the first run, so every capability stays Unknown).
    /// Repeating replaces the kernel snapshot (no duplicates).
    BootstrapResult run();

    const BootstrapResult &last() const { return last_; }
    std::shared_ptr<const flux::context::CapabilityContext> context() const { return context_; }
    void set_observer(BootstrapObserver observer) { observer_ = std::move(observer); }

  private:
    std::shared_ptr<flux::context::CapabilityContext> context_;
    Probe probe_;
    BootstrapObserver observer_;
    BootstrapResult last_;
    int attempts_ = 0;
};

/// The device probe: read-only fs at `root` ("" = "/"), built-in adapters, `hint` from properties.
CapabilityBootstrap::Probe make_device_probe(std::string root, std::function<PlatformHint()> hint);

} // namespace flux::kernel
