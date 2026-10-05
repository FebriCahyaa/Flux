#include "PolicyDecision.hpp"

#include <sstream>

namespace flux::policy {

const char *to_string(Action a) {
    switch (a) {
    case Action::NoAction: return "NO_ACTION";
    case Action::Observe: return "OBSERVE";
    case Action::Mitigate: return "MITIGATE";
    case Action::Boost: return "BOOST";
    case Action::Restore: return "RESTORE";
    }
    return "OBSERVE";
}

std::string explain(const PolicyDecision &d) {
    std::ostringstream o;
    o << "decision " << d.decision_id << ": " << to_string(d.action) << " (target "
      << (d.target.empty() ? "none" : d.target) << ", confidence " << flux::context::to_string(d.confidence)
      << ") - recommendation only, not executed\n";
    o << "session: " << (d.session_id.empty() ? "none" : d.session_id) << " evaluated_at_ms: " << d.evaluation_time_ms << "\n";
    o << "reason: " << d.reason << "\n";
    for (const auto &e : d.supporting_evidence) o << "  + " << e.source << " " << e.ref << "=" << e.value << "\n";
    for (const auto &e : d.blocking_evidence) o << "  - " << e.source << " " << e.ref << "=" << e.value << "\n";
    for (const auto &c : d.constraints) o << "  * " << to_string(c.kind) << " " << c.subject << ": " << c.detail << "\n";
    for (const auto &l : d.limitations) o << "  limitation: " << l << "\n";
    return o.str();
}

} // namespace flux::policy
