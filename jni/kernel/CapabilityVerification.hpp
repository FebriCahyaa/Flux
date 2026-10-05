// Capability verification foundation (Step 8.14).
//
// A capability becomes verified=true only after a complete, transactional proof:
//   read current -> validate -> snapshot -> safe reversible test value -> write -> exact read-back
//   -> restore original -> exact final read-back.
// Writes go through the existing Transaction Engine (NodeWriteOperation: snapshot, write-ahead
// journal, apply, verify, restore by read-back) — no second transaction framework.
//
// Only explicit, trusted verifier adapters may write, and only to their own fixed interface
// pattern; there is no generic "write a value to a path" path. Thermal capabilities are never
// written. Independent of the Decision Engine: this step neither decides nor executes policy.
#pragma once

#include "CapabilityContext.hpp"
#include "Transaction.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace flux::kernel {

struct VerificationResult {
    std::string capability_id;
    std::string verifier;     // adapter name, "" when none
    bool attempted = false;   // a write was tried
    bool verified = false;
    std::string before_value, test_value, readback_value, final_value;
    bool restored = false;    // final read-back equals before_value
    flux::context::Confidence confidence = flux::context::Confidence::None;
    flux::context::Risk risk = flux::context::Risk::Unknown;
    std::string reason;       // "verified" or the precise failure / refusal reason
};

/// Reads sibling nodes for an adapter (absolute paths, through the same Io).
using SiblingReader = std::function<std::optional<std::string>(const std::string &absolute_path)>;

/// An explicit, trusted verification method for one kind of control.
class VerifierAdapter {
  public:
    virtual ~VerifierAdapter() = default;
    virtual std::string name() const = 0;
    /// True when `interface` (relative, canonical, e.g. "sys/devices/.../scaling_max_freq") is this
    /// adapter's exact target pattern.
    virtual bool matches(const std::string &interface) const = 0;
    /// A safe, reversible value different from `current`, or nullopt (=> no_safe_test_value).
    /// `current` is already trimmed; nullopt also when `current` is not valid for this control.
    virtual std::optional<std::string> test_value(const std::string &absolute_path, const std::string &current,
                                                  const SiblingReader &read) const = 0;
};

/// cpufreq scaling_max_freq, KGSL max_gpuclk, devfreq max_freq, block read_ahead_kb, vm.swappiness.
std::vector<std::unique_ptr<VerifierAdapter>> builtin_verifiers();

class CapabilityVerifier {
  public:
    /// precondition: returns a reason to refuse every verification (e.g. Synrei actively managing
    /// thermal state), or nullopt to allow.
    CapabilityVerifier(flux::runtime::Io io, std::vector<std::unique_ptr<VerifierAdapter>> adapters,
                       flux::runtime::Transaction::JournalSink journal = nullptr,
                       flux::runtime::TxObserver observer = nullptr,
                       std::function<std::optional<std::string>()> precondition = nullptr);

    /// One attempt per capability id per verifier lifetime (one daemon run): a repeated call
    /// returns the first result without any I/O.
    VerificationResult verify(const flux::context::CapabilityFact &fact);
    /// Every fact of `publisher`, in id order (deterministic).
    std::vector<VerificationResult> verify_all(const flux::context::CapabilityContext &context,
                                               const std::string &publisher = "kernel");
    const std::map<std::string, VerificationResult> &results() const { return results_; }

  private:
    VerificationResult run(const flux::context::CapabilityFact &fact);
    flux::runtime::Io io_;
    std::vector<std::unique_ptr<VerifierAdapter>> adapters_;
    flux::runtime::Transaction::JournalSink journal_;
    flux::runtime::TxObserver observer_;
    std::function<std::optional<std::string>()> precondition_;
    std::map<std::string, VerificationResult> results_;
    uint64_t seq_ = 0;
};

/// Republishes `publisher`'s facts with verified=true for each verified result; every other
/// field and every other fact is preserved. Returns the number of facts updated.
size_t apply_verification(flux::context::CapabilityContext &context, const std::vector<VerificationResult> &results,
                          const std::string &publisher = "kernel");

/// True for thermal capabilities (domain thermal, thermal / cooling-device interfaces): never written.
bool is_thermal(const flux::context::CapabilityFact &fact);

} // namespace flux::kernel
