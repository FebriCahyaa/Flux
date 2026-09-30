#include "CapabilityContext.hpp"

#include <algorithm>
#include <set>

namespace flux::context {

const char *to_string(Support s) {
    switch (s) {
    case Support::Yes: return "yes";
    case Support::No: return "no";
    case Support::Unknown: break;
    }
    return "unknown";
}
const char *to_string(Confidence c) {
    switch (c) {
    case Confidence::Low: return "low";
    case Confidence::Medium: return "medium";
    case Confidence::High: return "high";
    case Confidence::None: break;
    }
    return "unknown"; // None is the UNKNOWN confidence level
}
const char *to_string(Risk r) {
    switch (r) {
    case Risk::Low: return "low";
    case Risk::Medium: return "medium";
    case Risk::High: return "high";
    case Risk::Unknown: break;
    }
    return "unknown";
}

void CapabilityContext::publish(const std::string &publisher, std::vector<CapabilityFact> facts) {
    facts.erase(std::remove_if(facts.begin(), facts.end(), [](const CapabilityFact &f) { return f.id.empty(); }),
                facts.end());
    for (auto &f : facts) f.publisher = publisher;
    by_source_[publisher] = std::move(facts);
    ++generation_;
    if (!observer_) return;

    ContextNotice n;
    n.generation = generation_;
    n.publisher = publisher;
    for (const auto &f : by_source_[publisher]) {
        ++n.facts;
        if (f.support == Support::Yes) ++n.supported;
        else if (f.support == Support::No) ++n.unsupported;
        else ++n.unknown;
        if (resolve(f.id).conflict) ++n.conflicts;
    }
    try {
        observer_(n);
    } catch (...) {
        // The context never fails because an observer did.
    }
}

std::vector<const CapabilityFact *> CapabilityContext::facts(const std::string &id) const {
    std::vector<const CapabilityFact *> out;
    for (const auto &[_, list] : by_source_)
        for (const auto &f : list)
            if (f.id == id) out.push_back(&f);
    return out;
}

Resolution CapabilityContext::resolve(const std::string &id) const {
    Resolution r;
    const CapabilityFact *best = nullptr;
    bool tie_disagrees = false;
    std::set<Support> stated;
    for (const auto *f : facts(id)) {
        r.publishers.push_back(f->publisher);
        if (f->support == Support::Unknown) continue; // says nothing; never counts as an answer
        stated.insert(f->support);
        if (!best || f->confidence > best->confidence) {
            best = f;
            tie_disagrees = false;
        } else if (f->confidence == best->confidence && f->support != best->support) {
            tie_disagrees = true;
        }
    }
    r.conflict = stated.size() > 1;
    if (!best || tie_disagrees) return r; // Unknown
    r.support = best->support;
    r.fact = best;
    return r;
}

std::vector<std::string> CapabilityContext::ids(const std::string &domain) const {
    std::set<std::string> out;
    for (const auto &[_, list] : by_source_)
        for (const auto &f : list)
            if (domain.empty() || f.domain == domain) out.insert(f.id);
    return {out.begin(), out.end()};
}

std::vector<std::string> CapabilityContext::publishers() const {
    std::vector<std::string> out;
    for (const auto &[name, _] : by_source_) out.push_back(name);
    return out;
}

} // namespace flux::context
