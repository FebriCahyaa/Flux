#include "CapabilityVerification.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <regex>
#include <sstream>

namespace flux::kernel {

namespace cx = flux::context;
namespace rt = flux::runtime;

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

std::vector<uint64_t> uint_list(const std::optional<std::string> &text) {
    std::vector<uint64_t> out;
    if (!text) return out;
    std::istringstream in(*text);
    std::string w;
    while (in >> w)
        if (auto v = to_uint(w)) out.push_back(*v);
    return out;
}

std::string dir_of(const std::string &path) { return path.substr(0, path.rfind('/')); }

/// Largest listed value strictly below `cur` and at least `floor` (lowering a ceiling is the safe direction).
std::optional<std::string> next_lower(const std::vector<uint64_t> &available, uint64_t cur, uint64_t floor) {
    std::optional<uint64_t> best;
    for (uint64_t f : available)
        if (f < cur && f >= floor && f > 0 && (!best || f > *best)) best = f;
    if (!best) return std::nullopt;
    return std::to_string(*best);
}

class PatternAdapter : public VerifierAdapter {
  public:
    PatternAdapter(std::string name, const char *pattern) : name_(std::move(name)), re_(pattern) {}
    std::string name() const override { return name_; }
    bool matches(const std::string &interface) const override { return std::regex_match(interface, re_); }

  private:
    std::string name_;
    std::regex re_;
};

/// cpufreq policy ceiling: next lower frequency from scaling_available_frequencies, not below scaling_min_freq.
class CpufreqMaxFreq final : public PatternAdapter {
  public:
    CpufreqMaxFreq()
        : PatternAdapter("cpufreq_scaling_max_freq", R"(sys/devices/system/cpu/cpufreq/policy[0-9]+/scaling_max_freq)") {}
    std::optional<std::string> test_value(const std::string &path, const std::string &current,
                                          const SiblingReader &read) const override {
        const auto cur = to_uint(current);
        if (!cur) return std::nullopt;
        const auto dir = dir_of(path);
        const auto min = uint_list(read(dir + "/scaling_min_freq"));
        return next_lower(uint_list(read(dir + "/scaling_available_frequencies")), *cur, min.empty() ? 0 : min[0]);
    }
};

/// Adreno KGSL ceiling: next lower frequency from gpu_available_frequencies.
class KgslMaxGpuclk final : public PatternAdapter {
  public:
    KgslMaxGpuclk() : PatternAdapter("kgsl_max_gpuclk", R"(sys/class/kgsl/kgsl-3d0/max_gpuclk)") {}
    std::optional<std::string> test_value(const std::string &path, const std::string &current,
                                          const SiblingReader &read) const override {
        const auto cur = to_uint(current);
        if (!cur) return std::nullopt;
        return next_lower(uint_list(read(dir_of(path) + "/gpu_available_frequencies")), *cur, 1);
    }
};

/// devfreq ceiling: next lower frequency from available_frequencies, not below min_freq.
class DevfreqMaxFreq final : public PatternAdapter {
  public:
    DevfreqMaxFreq() : PatternAdapter("devfreq_max_freq", R"(sys/class/devfreq/[A-Za-z0-9_.:,+-]+/max_freq)") {}
    std::optional<std::string> test_value(const std::string &path, const std::string &current,
                                          const SiblingReader &read) const override {
        const auto cur = to_uint(current);
        if (!cur) return std::nullopt;
        const auto dir = dir_of(path);
        const auto min = uint_list(read(dir + "/min_freq"));
        return next_lower(uint_list(read(dir + "/available_frequencies")), *cur, min.empty() ? 0 : min[0]);
    }
};

/// Block read-ahead: halve (or +8 KiB when small); bounded, harmless, exactly reversible.
class BlockReadAhead final : public PatternAdapter {
  public:
    BlockReadAhead() : PatternAdapter("block_read_ahead_kb", R"(sys/block/[a-z0-9]+/queue/read_ahead_kb)") {}
    std::optional<std::string> test_value(const std::string &, const std::string &current,
                                          const SiblingReader &) const override {
        const auto cur = to_uint(current);
        if (!cur || *cur > 65536) return std::nullopt;
        return std::to_string(*cur >= 16 ? *cur / 2 : *cur + 8);
    }
};

/// vm.swappiness: one step down (or up from 0); exactly reversible.
class VmSwappiness final : public PatternAdapter {
  public:
    VmSwappiness() : PatternAdapter("vm_swappiness", R"(proc/sys/vm/swappiness)") {}
    std::optional<std::string> test_value(const std::string &, const std::string &current,
                                          const SiblingReader &) const override {
        const auto cur = to_uint(current);
        if (!cur || *cur > 200) return std::nullopt;
        return std::to_string(*cur > 0 ? *cur - 1 : 1);
    }
};

bool safe_interface(const std::string &p) {
    if (p.empty() || p.front() == '/' || p.find("..") != std::string::npos || p.find("//") != std::string::npos)
        return false;
    return std::all_of(p.begin(), p.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '/' || c == ':' || c == ',' || c == '+' || c == '-';
    });
}

} // namespace

std::vector<std::unique_ptr<VerifierAdapter>> builtin_verifiers() {
    std::vector<std::unique_ptr<VerifierAdapter>> v;
    v.push_back(std::make_unique<CpufreqMaxFreq>());
    v.push_back(std::make_unique<KgslMaxGpuclk>());
    v.push_back(std::make_unique<DevfreqMaxFreq>());
    v.push_back(std::make_unique<BlockReadAhead>());
    v.push_back(std::make_unique<VmSwappiness>());
    return v;
}

bool is_thermal(const cx::CapabilityFact &f) {
    return f.domain == "thermal" || f.id.rfind("thermal.", 0) == 0 || f.interface.find("thermal") != std::string::npos ||
           f.interface.find("cooling_device") != std::string::npos;
}

CapabilityVerifier::CapabilityVerifier(rt::Io io, std::vector<std::unique_ptr<VerifierAdapter>> adapters,
                                       rt::Transaction::JournalSink journal, rt::TxObserver observer,
                                       std::function<std::optional<std::string>()> precondition)
    : io_(std::move(io)), adapters_(std::move(adapters)), journal_(std::move(journal)), observer_(std::move(observer)),
      precondition_(std::move(precondition)) {}

VerificationResult CapabilityVerifier::verify(const cx::CapabilityFact &fact) {
    if (auto it = results_.find(fact.id); it != results_.end()) return it->second; // once per lifetime
    VerificationResult r;
    try {
        r = run(fact);
    } catch (const std::exception &e) {
        r.capability_id = fact.id;
        r.risk = fact.risk;
        r.reason = std::string("verifier_error: ") + e.what();
    } catch (...) {
        r.capability_id = fact.id;
        r.risk = fact.risk;
        r.reason = "verifier_error";
    }
    results_[fact.id] = r;
    return r;
}

VerificationResult CapabilityVerifier::run(const cx::CapabilityFact &fact) {
    VerificationResult r;
    r.capability_id = fact.id;
    r.risk = fact.risk;
    auto refuse = [&](const std::string &why) {
        r.reason = why;
        return r;
    };
    // Gating: nothing is read or written for a refused capability.
    if (is_thermal(fact)) return refuse("thermal_protected");
    if (fact.support != cx::Support::Yes) return refuse("unsupported");
    if (!fact.readable) return refuse("unreadable");
    if (!fact.writable) return refuse("not_writable");
    if (!fact.rollback) return refuse("no_rollback");
    if (fact.risk == cx::Risk::Unknown) return refuse("unknown_risk");
    if (fact.risk == cx::Risk::High) return refuse("high_risk");
    if (!safe_interface(fact.interface)) return refuse("unsafe_interface");
    const VerifierAdapter *adapter = nullptr;
    for (const auto &a : adapters_)
        if (a->matches(fact.interface)) adapter = a.get();
    if (!adapter) return refuse("no_safe_verifier");
    r.verifier = adapter->name();
    if (precondition_)
        if (auto block = precondition_()) return refuse("precondition: " + *block);

    const std::string path = "/" + fact.interface;
    if (!io_.exists || !io_.read || !io_.write || !io_.exists(path)) return refuse("unreadable");
    const auto raw = io_.read(path);
    if (!raw) return refuse("unreadable");
    r.before_value = trim(*raw);
    if (!to_uint(r.before_value)) return refuse("invalid_current_value");
    const SiblingReader sibling = [this](const std::string &p) -> std::optional<std::string> {
        if (!io_.exists(p)) return std::nullopt;
        auto v = io_.read(p);
        return v ? std::optional<std::string>(trim(*v)) : std::nullopt;
    };
    const auto test = adapter->test_value(path, r.before_value, sibling);
    if (!test || *test == r.before_value) return refuse("no_safe_test_value");
    r.test_value = *test;

    // Transactional proof through the existing engine. Writes are counted to tell a rejected write
    // from a value that did not stick.
    int writes = 0;
    bool apply_ok = false, restore_ok = false;
    rt::Io io = io_;
    io.write = [this, &writes, &apply_ok, &restore_ok](const std::string &p, const std::string &v) {
        const bool ok = io_.write(p, v);
        (++writes == 1 ? apply_ok : restore_ok) = ok;
        return ok;
    };
    std::optional<std::string> mismatch;
    const rt::TxObserver observer = [&](const rt::TxNotice &n) {
        if (n.kind == rt::TxNotice::Kind::Verify && !n.ok) mismatch = io_.read(path); // before rollback
        if (observer_) observer_(n);
    };
    rt::RuntimePlan plan;
    plan.domain = "capability_verification";
    plan.subject = fact.id;
    plan.operations.push_back(std::make_unique<rt::NodeWriteOperation>(io, "verification", path, r.test_value));
    rt::Transaction tx(rt::make_transaction_id(0, ++seq_), std::move(plan), journal_, observer);

    auto final_read = [&] {
        const auto v = io_.read(path);
        r.final_value = v ? trim(*v) : "";
        r.restored = v && r.final_value == r.before_value;
    };
    if (!tx.start()) {
        if (!tx.skipped().empty()) return refuse("unreadable");
        for (const auto &l : tx.log())
            if (l.rfind("journal write failed", 0) == 0) return refuse("journal_failed"); // nothing written
        r.attempted = true;
        r.reason = apply_ok ? "readback_mismatch" : "write_rejected";
        if (mismatch) r.readback_value = trim(*mismatch);
        final_read();
        return r;
    }
    r.attempted = true;
    if (const auto v = io_.read(path)) r.readback_value = trim(*v);
    const bool clean = tx.finish();
    final_read();
    if (!clean || !r.restored) {
        r.restored = false;
        r.reason = restore_ok ? "restore_readback_mismatch" : "restore_write_rejected";
        return r;
    }
    if (r.readback_value != r.test_value) return refuse("readback_mismatch");
    r.verified = true;
    r.confidence = cx::Confidence::High;
    r.reason = "verified";
    return r;
}

std::vector<VerificationResult> CapabilityVerifier::verify_all(const cx::CapabilityContext &context,
                                                               const std::string &publisher) {
    std::vector<VerificationResult> out;
    for (const auto &id : context.ids())
        for (const auto *f : context.facts(id))
            if (f->publisher == publisher) out.push_back(verify(*f));
    return out;
}

size_t apply_verification(cx::CapabilityContext &context, const std::vector<VerificationResult> &results,
                          const std::string &publisher) {
    std::map<std::string, const VerificationResult *> ok;
    for (const auto &r : results)
        if (r.verified && r.attempted && r.restored && r.reason == "verified") ok[r.capability_id] = &r;
    if (ok.empty()) return 0;
    std::vector<cx::CapabilityFact> facts;
    size_t updated = 0;
    for (const auto &id : context.ids())
        for (const auto *f : context.facts(id)) {
            if (f->publisher != publisher) continue;
            cx::CapabilityFact copy = *f; // every field preserved
            if (auto it = ok.find(id); it != ok.end() && copy.writable && copy.readable) {
                copy.verified = true;
                copy.note += std::string(copy.note.empty() ? "" : "; ") + "verified by write/read-back/restore (" +
                             it->second->verifier + ")";
                ++updated;
            }
            facts.push_back(std::move(copy));
        }
    if (updated) context.publish(publisher, std::move(facts));
    return updated;
}

} // namespace flux::kernel
