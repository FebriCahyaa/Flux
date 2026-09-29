#include "ZygiskBackend.hpp"

namespace flux::compat {

void ZygiskBackend::configure(const EffectiveProfile &p, int pid, int) {
    pid_ = pid;
    error_.clear();
    report_ = ProviderReport{};
    tx_.clear();
    scope_ = provider::ProcessScope{};
    if (p.process_scope == "all") scope_.kind = provider::ProcessScope::Kind::All;
    else if (p.process_scope == "listed") scope_.kind = provider::ProcessScope::Kind::Listed;
    scope_.processes = p.processes;
    profile_name_ = p.chain.empty() ? "game" : p.chain.back();
}

bool ZygiskBackend::prepare(const Resolution &plan, const ProfileLibrary &lib) {
    report_.state = to_string(a_.provider_state());
    std::string err;
    tx_ = a_.arm(plan, lib, scope_, profile_name_, err);
    if (tx_.empty()) {
        error_ = err.empty() ? "no identity layer to arm" : err;
        report_.reason = error_;
        report_.state = err.empty() ? report_.state : "failed";
        return false;
    }
    report_.transaction_id = tx_;
    return true;
}

bool ZygiskBackend::apply(const Resolution &) { return !tx_.empty(); }

bool ZygiskBackend::verify(const Resolution &) {
    auto fail = [&](const std::string &why, const char *state) {
        error_ = why;
        report_.state = state;
        report_.reason = why;
        return false;
    };
    if (pid_ <= 1) return fail("no process to verify", "failed");

    auto st = a_.process_status(pid_);
    if (!st) {
        // The plan is armed for the NEXT launch. This process either started before that, or the
        // provider never saw it (Zygisk disabled, module not loaded).
        return fail(a_.loaded() ? "the game started before its profile was armed; relaunch the game"
                                : "the provider has not loaded in this boot (is Zygisk enabled? reboot after installing)",
                    a_.loaded() ? "armed" : "installed");
    }
    report_.transaction_id = st->transaction_id;
    report_.layers.clear();
    for (const auto &l : st->layers) report_.layers.emplace_back(to_string(l.layer), l.state);

    if (st->transaction_id != tx_)
        return fail("the game runs an older profile (armed " + st->transaction_id + ", now " + tx_ + "); relaunch the game", "failed");
    if (st->state == provider::ProcState::Failed || st->state == provider::ProcState::Ended) {
        std::string why = st->state == provider::ProcState::Ended ? "the process ended" : "the provider refused this process";
        for (const auto &l : st->layers)
            if (!l.detail.empty()) { why += ": " + l.detail; break; }
        return fail(why, "failed");
    }
    if (st->state == provider::ProcState::Matched)
        return fail("the provider matched the process but has not reported applying anything", "matched");

    report_.state = st->state == provider::ProcState::Verified ? "verified" : "applied";
    report_.reason.clear();
    error_.clear();
    return true;
}

} // namespace flux::compat
