#include "CapabilityBootstrap.hpp"

#include <exception>
#include <stdexcept>

namespace flux::kernel {

const char *to_string(BootstrapStatus s) {
    switch (s) {
    case BootstrapStatus::Ok: return "ok";
    case BootstrapStatus::Empty: return "empty";
    case BootstrapStatus::Failed: return "failed";
    case BootstrapStatus::NotRun: break;
    }
    return "not_run";
}

CapabilityBootstrap::CapabilityBootstrap(std::shared_ptr<flux::context::CapabilityContext> context, Probe probe)
    : context_(std::move(context)), probe_(std::move(probe)) {}

BootstrapResult CapabilityBootstrap::run() {
    BootstrapResult r;
    r.attempt = ++attempts_;
    try {
        if (!context_) throw std::runtime_error("no capability context");
        if (!probe_) throw std::runtime_error("no probe");
        auto facts = export_facts(probe_());
        r.facts = facts.size();
        for (const auto &f : facts)
            if (f.support == flux::context::Support::Yes && f.domain != "kernel") ++r.supported;
        context_->publish(kContextPublisher, std::move(facts));
        r.status = r.supported ? BootstrapStatus::Ok : BootstrapStatus::Empty;
    } catch (const std::exception &e) {
        r = {BootstrapStatus::Failed, r.attempt, 0, 0, 0, e.what()};
    } catch (...) {
        r = {BootstrapStatus::Failed, r.attempt, 0, 0, 0, "unknown probe failure"};
    }
    r.generation = context_ ? context_->generation() : 0;
    last_ = r;
    if (observer_) {
        try {
            observer_(r);
        } catch (...) {
            // Observatory problems never affect the bootstrap.
        }
    }
    return r;
}

CapabilityBootstrap::Probe make_device_probe(std::string root, std::function<PlatformHint()> hint) {
    return [root = std::move(root), hint = std::move(hint)] {
        auto fs = make_readonly_fs(root);
        return observe(*fs, hint ? hint() : PlatformHint{}, AdapterRegistry::with_builtin());
    };
}

} // namespace flux::kernel
