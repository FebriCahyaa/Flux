// Shared capability context (Step 7.5) — the one place capability facts are published and read.
//
// Producers (Kernel Intelligence today; graphics and Synrei thermal later) publish facts under their
// source name. Consumers (Performance Planner, future Graphics Engine, future Synrei Thermal Engine)
// read them through `resolve()`. The context never probes, never writes, and never infers support:
// an id nobody published, or one whose equally confident sources disagree, is Unknown.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace flux::context {

enum class Support { Unknown, No, Yes };
/// HIGH / MEDIUM / LOW / UNKNOWN; `None` is UNKNOWN (to_string -> "unknown").
enum class Confidence { None, Low, Medium, High };
enum class Risk { Unknown, Low, Medium, High };

const char *to_string(Support s);
const char *to_string(Confidence c);
const char *to_string(Risk r);

/// One producer's statement about one capability. Every field is carried as published.
struct CapabilityFact {
    std::string id;
    std::string domain;
    std::string source;    // what determined it (e.g. adapter "qualcomm"); preserved as given
    std::string publisher; // who published it (e.g. "kernel"); set by the context
    Support support = Support::Unknown;
    bool readable = false;
    bool writable = false; // permission hint as the producer reported it
    bool verified = false;
    Confidence confidence = Confidence::None;
    Risk risk = Risk::Unknown;
    bool rollback = false;
    bool requires_adapter = false;
    std::string interface;
    std::string value;
    std::string range;
    std::string note;
};

/// Outcome of reading one id across all sources.
struct Resolution {
    Support support = Support::Unknown;
    const CapabilityFact *fact = nullptr; // the fact the answer is taken from; null when Unknown
    bool conflict = false;                // sources disagree on support
    std::vector<std::string> publishers;  // every publisher that stated this id
};

/// What the context tells an observer after a publish (Observatory integration interface only;
/// nothing stores it yet).
struct ContextNotice {
    uint64_t generation = 0;
    std::string publisher;
    int facts = 0, supported = 0, unsupported = 0, unknown = 0, conflicts = 0;
};
using ContextObserver = std::function<void(const ContextNotice &)>;

class CapabilityContext {
  public:
    /// Replaces every fact previously published by `publisher` (a snapshot, not a merge).
    /// Facts with an empty id are dropped; `source` is kept, `publisher` is set.
    void publish(const std::string &publisher, std::vector<CapabilityFact> facts);

    /// Highest-confidence fact among sources that state Yes/No. Equal top confidence with
    /// different answers -> Unknown + conflict; lower-confidence disagreement is flagged, not decisive. Fields are never merged across sources.
    Resolution resolve(const std::string &id) const;
    Support supports(const std::string &id) const { return resolve(id).support; }

    std::vector<const CapabilityFact *> facts(const std::string &id) const;
    std::vector<std::string> ids(const std::string &domain = "") const;
    std::vector<std::string> publishers() const;
    uint64_t generation() const { return generation_; }

    /// Called after a publish; exceptions from the observer are swallowed.
    void set_observer(ContextObserver observer) { observer_ = std::move(observer); }

  private:
    std::map<std::string, std::vector<CapabilityFact>> by_source_;
    uint64_t generation_ = 0;
    ContextObserver observer_;
};

} // namespace flux::context
