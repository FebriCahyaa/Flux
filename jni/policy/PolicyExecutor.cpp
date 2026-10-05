#include "PolicyExecutor.hpp"

#include "PerformancePlanner.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <regex>
#include <sstream>

namespace flux::policy {

namespace cx = flux::context;
namespace rt = flux::runtime;

const char *to_string(ExecutionStatus s) {
    switch (s) {
    case ExecutionStatus::NotExecuted: return "not_executed";
    case ExecutionStatus::Blocked: return "blocked";
    case ExecutionStatus::NoChange: return "no_change";
    case ExecutionStatus::AlreadyActive: return "already_active";
    case ExecutionStatus::Applied: return "applied";
    case ExecutionStatus::ApplyFailed: return "apply_failed";
    case ExecutionStatus::VerifyFailed: return "verify_failed";
    case ExecutionStatus::Restored: return "restored";
    case ExecutionStatus::RestoreFailed: return "restore_failed";
    case ExecutionStatus::NothingToRestore: return "nothing_to_restore";
    }
    return "blocked";
}

std::string to_text(const PolicyExecutionResult &r) {
    std::ostringstream o;
    o << "execution " << r.execution_id << ": " << to_string(r.requested_action) << " -> " << to_string(r.final_status)
      << " executed=" << r.executed << " verified=" << r.verified << " rolled_back=" << r.rolled_back
      << " restored=" << r.restored << " tx=" << (r.transaction_id.empty() ? "none" : r.transaction_id) << "\n";
    o << "reason: " << r.reason << "\n";
    for (const auto &a : r.affected_capabilities)
        o << "  ~ " << a.capability_id << " [" << a.operation << "] " << a.path << ": " << a.before << " -> " << a.target
          << " (read back " << a.after << ")\n";
    for (const auto &b : r.blocked_constraints) o << "  ! " << b << "\n";
    for (const auto &l : r.limitations) o << "  limitation: " << l << "\n";
    return o.str();
}

namespace {

std::string trim(const std::string &s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::optional<uint64_t> to_uint(const std::string &s) {
    if (s.empty() || s.size() > 19 || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }))
        return std::nullopt;
    return std::stoull(s);
}

/// A trusted operation: a fixed interface pattern and the single, capability-listed step it may
/// take for MITIGATE (next lower listed frequency) or BOOST (highest listed frequency).
struct TrustedOperation {
    const char *name;
    const char *resource;
    const char *domain;
    std::regex pattern;
    const char *list;     // sibling with the available frequencies
    const char *min_node; // sibling floor ("" = lowest listed)
    const char *max_node; // sibling ceiling ("" = highest listed)
};

const std::vector<TrustedOperation> &operations() {
    static const std::vector<TrustedOperation> ops = {
        {"cpufreq_scaling_max_freq", "cpu", "cpufreq",
         std::regex(R"(sys/devices/system/cpu/cpufreq/policy[0-9]+/scaling_max_freq)"), "scaling_available_frequencies",
         "scaling_min_freq", "cpuinfo_max_freq"},
        {"kgsl_max_gpuclk", "gpu", "gpu", std::regex(R"(sys/class/kgsl/kgsl-3d0/max_gpuclk)"), "gpu_available_frequencies",
         "", ""},
        {"devfreq_max_freq", "gpu", "gpu", std::regex(R"(sys/class/devfreq/[A-Za-z0-9_.:,+-]+/max_freq)"),
         "available_frequencies", "min_freq", ""},
    };
    return ops;
}

bool safe_interface(const std::string &p) {
    if (p.empty() || p.front() == '/' || p.find("..") != std::string::npos || p.find("//") != std::string::npos)
        return false;
    return std::all_of(p.begin(), p.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '/' || c == ':' || c == ',' || c == '+' || c == '-';
    });
}

bool thermal_like(const cx::CapabilityFact &f) {
    return f.domain == "thermal" || f.interface.find("thermal") != std::string::npos ||
           f.interface.find("cooling_device") != std::string::npos;
}

struct Planned {
    AffectedCapability cap;
    bool change = false;
};

} // namespace

PolicyExecutor::PolicyExecutor(rt::Io io, rt::Transaction::JournalSink journal, rt::TxObserver observer)
    : io_(std::move(io)), journal_(std::move(journal)), observer_(std::move(observer)) {}

PolicyExecutor::~PolicyExecutor() = default;

PolicyExecutionResult PolicyExecutor::execute(const PolicyDecision &d, const ExecutionContext &x) {
    PolicyExecutionResult r;
    r.execution_id = "px-" + d.decision_id;
    r.requested_action = d.action;
    r.limitations.push_back("executes the approved decision only; no escalation, no adaptation");
    auto block = [&](const std::string &reason) {
        r.final_status = ExecutionStatus::Blocked;
        r.blocked_constraints.push_back(reason);
        if (r.reason.empty()) r.reason = "blocked before the Transaction Engine: " + reason;
    };

    try {
        switch (d.action) {
        case Action::NoAction:
            r.reason = "NO_ACTION: nothing to execute";
            return r;
        case Action::Observe:
            r.reason = "OBSERVE: the decision is to observe; nothing executed";
            return r;
        case Action::Restore: {
            if (!active_) {
                r.final_status = ExecutionStatus::NothingToRestore;
                r.reason = "no executor-owned transaction; GameRuntime and journal recovery restore their own";
                return r;
            }
            r.transaction_id = active_->id();
            r.executed = true;
            const bool clean = active_->finish(); // existing restore: write back, decide by read-back
            r.restored = clean;
            r.final_status = clean ? ExecutionStatus::Restored : ExecutionStatus::RestoreFailed;
            r.reason = clean ? "restored and read back" : "not every value read back at its snapshot; journal kept";
            active_.reset();
            active_key_.clear();
            return r;
        }
        case Action::Mitigate:
        case Action::Boost: break;
        }

        // -- decision integrity: execute exactly what was approved --------------------------------
        if (d.decision_id.empty()) block("invalid_decision: no decision_id");
        const TrustedOperation *any = nullptr;
        for (const auto &op : operations())
            if (d.target == op.resource) any = &op;
        if (!any) {
            const bool known = d.target == "memory" || d.target == "storage" || d.target == "display" ||
                               d.target == "thermal";
            block(known ? "no_trusted_adapter: no execution operation for " + d.target : "invalid_target: " + d.target);
        }
        for (const auto &c : d.constraints) {
            const bool hard = c.kind == ConstraintKind::RestoreRequired || c.kind == ConstraintKind::CapabilityBlocked ||
                              c.kind == ConstraintKind::CapabilityRestricted || c.kind == ConstraintKind::Conflict ||
                              c.kind == ConstraintKind::ThermalUnknown || c.kind == ConstraintKind::History;
            const bool boost_only = d.action == Action::Boost &&
                                    (c.kind == ConstraintKind::ThermalSafety ||
                                     (c.subject == "boost" && (c.kind == ConstraintKind::InsufficientEvidence ||
                                                               c.kind == ConstraintKind::ProfileIntent)));
            if (hard || boost_only) block(std::string("decision_constraint: ") + to_string(c.kind) + " " + c.subject);
        }
        // -- thermal: only Synrei's verified safety is used, never reinterpreted ------------------
        if (d.action == Action::Boost && x.thermal && x.thermal->readable && x.thermal->verified &&
            x.thermal->constraint == flux::thermal::Constraint::Constrained)
            block("thermal_safety: Synrei reports " + x.thermal->state);
        // -- runtime state (existing GameRuntime / Transaction contracts) -------------------------
        if (!x.runtime.game_active) block("no_active_game");
        if (x.session_id.empty() || x.session_id != d.session_id) block("stale_decision: decision for another session");
        if (x.runtime.recovery_failed) block("recovery_incomplete");
        if (x.runtime.transaction == rt::TxState::Failed || x.runtime.transaction == rt::TxState::Restoring)
            block(std::string("invalid_transaction_state: ") + rt::to_string(x.runtime.transaction));
        const std::string key = std::string(to_string(d.action)) + ":" + d.target;
        if (active_) {
            if (active_key_ == key && r.blocked_constraints.empty()) {
                r.final_status = ExecutionStatus::AlreadyActive;
                r.transaction_id = active_->id();
                r.reason = "already applied by transaction " + active_->id() + "; not executed again";
                return r;
            }
            block("conflicting_active_transaction: " + active_key_ + " must be restored first");
        }
        if (!x.capabilities) block("capability_unsupported: no capability context");
        if (!r.blocked_constraints.empty()) return r;

        // -- capability gating per trusted operation --------------------------------------------
        std::vector<Planned> planned;
        bool any_candidate = false;
        for (const auto &op : operations()) {
            if (d.target != op.resource) continue;
            for (const auto &id : x.capabilities->ids(op.domain)) {
                const auto res = x.capabilities->resolve(id);
                const auto all = x.capabilities->facts(id);
                const cx::CapabilityFact *f = res.fact ? res.fact : (all.empty() ? nullptr : all.front());
                if (!f || thermal_like(*f) || !safe_interface(f->interface) || !std::regex_match(f->interface, op.pattern))
                    continue; // not a trusted operation target: never executed
                any_candidate = true;
                const std::string path = "/" + f->interface;
                std::string why;
                if (res.support != cx::Support::Yes || !res.fact) why = "capability_unsupported";
                else if (!f->readable) why = "capability_unreadable";
                else if (!f->writable) why = "capability_not_writable";
                else if (!f->verified) why = "capability_unverified";
                else if (!f->rollback) why = "no_rollback";
                else if (f->risk != cx::Risk::Low && f->risk != cx::Risk::Medium) why = "unsafe_risk";
                else if (flux::perf::PerformancePlanner::interface_allowed(path)) why = "planner_owned_interface";
                if (!why.empty()) {
                    r.blocked_constraints.push_back(id + ": " + why);
                    continue;
                }
                // Target: one listed step, validated against the known range; never invented.
                const auto read = [this](const std::string &p) -> std::optional<std::string> {
                    if (!io_.exists || !io_.read || !io_.exists(p)) return std::nullopt;
                    auto v = io_.read(p);
                    return v ? std::optional<std::string>(trim(*v)) : std::nullopt;
                };
                const std::string dir = path.substr(0, path.rfind('/'));
                const auto current = read(path);
                std::vector<uint64_t> list;
                if (auto text = read(dir + "/" + op.list)) {
                    std::istringstream in(*text);
                    std::string w;
                    while (in >> w)
                        if (auto v = to_uint(w)) list.push_back(*v);
                }
                std::sort(list.begin(), list.end());
                list.erase(std::unique(list.begin(), list.end()), list.end());
                const auto cur = current ? to_uint(*current) : std::nullopt;
                if (!cur || list.empty()) {
                    r.blocked_constraints.push_back(id + ": invalid_target (no readable current value or frequency list)");
                    continue;
                }
                uint64_t lo = list.front(), hi = list.back();
                if (*op.min_node)
                    if (auto v = read(dir + "/" + op.min_node); v && to_uint(*v)) lo = std::max(lo, *to_uint(*v));
                if (*op.max_node)
                    if (auto v = read(dir + "/" + op.max_node); v && to_uint(*v)) hi = std::min(hi, *to_uint(*v));
                if (*cur < lo || *cur > hi) {
                    r.blocked_constraints.push_back(id + ": invalid_target (current " + std::to_string(*cur) +
                                                    " outside known range " + std::to_string(lo) + ".." +
                                                    std::to_string(hi) + ")");
                    continue;
                }
                std::optional<uint64_t> target;
                for (uint64_t v : list) {
                    if (v < lo || v > hi) continue;
                    if (d.action == Action::Mitigate && v < *cur) target = v;               // largest below current
                    if (d.action == Action::Boost && v > *cur && (!target || v > *target)) target = v; // highest
                }
                Planned pl;
                pl.cap = {id, op.name, path, std::to_string(*cur), target ? std::to_string(*target) : std::to_string(*cur), ""};
                pl.change = target.has_value();
                planned.push_back(pl);
            }
        }
        if (!any_candidate) {
            block("no_trusted_adapter: no trusted " + d.target + " operation in the capability context");
            return r;
        }
        std::vector<Planned> changes;
        for (const auto &pl : planned)
            if (pl.change) changes.push_back(pl);
        if (changes.empty()) {
            if (!planned.empty() && r.blocked_constraints.empty()) {
                r.final_status = ExecutionStatus::NoChange;
                r.reason = "every permitted control is already at the operation's limit";
                return r;
            }
            r.final_status = ExecutionStatus::Blocked;
            r.reason = "no permitted operation: " + (r.blocked_constraints.empty() ? std::string("none")
                                                                                   : r.blocked_constraints.front());
            return r;
        }
        if (!r.blocked_constraints.empty())
            r.limitations.push_back("some controls were not permitted and are excluded (see blocked constraints)");

        // -- RuntimePlan through the existing Transaction Engine ---------------------------------
        // The transaction outlives this call (it is kept until RESTORE), so its callbacks must not
        // refer to this stack frame: the counters live in a shared probe.
        struct Probe {
            int writes = 0;
            bool verify_failed = false, rolled_back = false, rollback_clean = false;
        };
        const auto probe = std::make_shared<Probe>();
        rt::Io io = io_;
        io.write = [this, probe](const std::string &p, const std::string &v) {
            ++probe->writes;
            return io_.write(p, v);
        };
        const rt::TxObserver observer = [this, probe](const rt::TxNotice &n) {
            if (n.kind == rt::TxNotice::Kind::Verify && !n.ok) probe->verify_failed = true;
            if (n.kind == rt::TxNotice::Kind::Rollback) {
                probe->rolled_back = true;
                probe->rollback_clean = n.ok;
            }
            if (observer_) observer_(n);
        };
        rt::RuntimePlan plan;
        plan.domain = "policy";
        plan.subject = d.decision_id;
        for (const auto &pl : changes)
            plan.operations.push_back(std::make_unique<rt::NodeWriteOperation>(
                io, std::string("policy:") + to_string(d.action), pl.cap.path, pl.cap.target));
        auto tx = std::make_unique<rt::Transaction>(rt::make_transaction_id(d.evaluation_time_ms, ++seq_), std::move(plan),
                                                    journal_, observer);
        r.transaction_id = tx->id();
        const bool ok = tx->start();
        for (auto pl : changes) {
            const auto v = io_.read(pl.cap.path);
            pl.cap.after = v ? trim(*v) : "unreadable";
            r.affected_capabilities.push_back(pl.cap);
        }
        r.executed = probe->writes > 0;
        if (ok) {
            r.final_status = ExecutionStatus::Applied;
            r.verified = true;
            r.reason = std::to_string(changes.size()) + " operation(s) applied and read back";
            active_ = std::move(tx);
            active_key_ = key;
            return r;
        }
        r.final_status = probe->verify_failed ? ExecutionStatus::VerifyFailed : ExecutionStatus::ApplyFailed;
        r.rolled_back = probe->rolled_back;
        r.restored = probe->rolled_back && probe->rollback_clean;
        r.reason = std::string(probe->verify_failed ? "read-back differed" : "a write was rejected") + "; rolled back" +
                   (r.restored ? " and read back at the snapshot" : ", NOT fully restored (journal kept)");
        return r;
    } catch (const std::exception &e) {
        r.final_status = ExecutionStatus::Blocked;
        r.reason = std::string("executor error: ") + e.what();
        return r;
    } catch (...) {
        r.final_status = ExecutionStatus::Blocked;
        r.reason = "executor error";
        return r;
    }
}

} // namespace flux::policy
