#pragma once

// The process-scoped execution backend. It decides nothing: the resolver's plan is armed for the
// provider (jni/zygisk) to apply inside the game process, and this backend then checks what the
// provider reports about that process. The provider is optional; without it identity layers are
// reported unavailable and the rest of Flux is unaffected.

#include "Arming.hpp"
#include "Runtime.hpp"

namespace flux::compat {

class ZygiskBackend final : public Backend {
public:
    explicit ZygiskBackend(Arming &arming) : a_(arming) {}

    const char *name() const override { return "zygisk"; }
    BackendState available() const override { return a_.backend_state(); }
    bool supports(const std::string &package) const override { return !package.empty() && package.find('/') == std::string::npos && package.find("..") == std::string::npos; }

    void configure(const EffectiveProfile &profile, int pid, int uid) override;
    /// Arm the plan for the next launch of this package (idempotent for an unchanged plan).
    bool prepare(const Resolution &plan, const ProfileLibrary &lib) override;
    bool apply(const Resolution &plan) override;
    /// Success only when the provider itself reports having applied THIS plan to THIS process.
    /// A merely armed plan proves nothing about a process that is already running.
    bool verify(const Resolution &plan) override;
    /// The process-local context ends with the process and the plan stays armed for the next
    /// launch, so there is nothing to undo here.
    bool restore(const Resolution &) override { return true; }

    std::string last_error() const override { return error_; }
    ProviderReport provider_report() const override { return report_; }

private:
    Arming &a_;
    std::string profile_name_ = "game";
    provider::ProcessScope scope_;
    int pid_ = 0;
    std::string tx_;
    std::string error_;
    ProviderReport report_;
};

} // namespace flux::compat
