#include "Explanation.hpp"

#include <cstdlib>
#include <sstream>

namespace flux::observatory {

std::string to_text(const Explanation &e) {
    std::ostringstream o;
    o << "[" << e.topic << "] " << e.summary << "\n";
    o << "  finding: " << e.primary_finding << " (confidence " << to_string(e.confidence) << ")\n";
    if (e.time_range.from_ms || e.time_range.to_ms)
        o << "  time_range: " << (e.time_range.from_ms ? std::to_string(*e.time_range.from_ms) : "unknown") << " .. "
          << (e.time_range.to_ms ? std::to_string(*e.time_range.to_ms) : "unknown") << "\n";
    if (!e.related_session_id.empty()) o << "  session: " << e.related_session_id << "\n";
    if (!e.related_transaction_id.empty()) o << "  transaction: " << e.related_transaction_id << "\n";
    for (const auto &r : e.supporting)
        o << "  + " << (r.derived ? "derived " : "") << r.event_type << " " << r.event_id << " " << r.field << "=" << r.value
          << "\n";
    for (const auto &r : e.contradicting)
        o << "  - " << (r.derived ? "derived " : "") << r.event_type << " " << r.event_id << " " << r.field << "=" << r.value
          << "\n";
    for (const auto &l : e.limitations) o << "  limitation: " << l << "\n";
    return o.str();
}

ParsedEvidence parse_evidence(const std::string &text) {
    ParsedEvidence p;
    const auto eq = text.find('=');
    const auto open = text.rfind(" (");
    if (eq == std::string::npos || open == std::string::npos || open < eq || text.empty() || text.back() != ')')
        return p;
    p.metric = text.substr(0, eq);
    std::string body = text.substr(eq + 1, open - eq - 1);
    const std::string origin = text.substr(open + 2, text.size() - open - 3); // "source @ts"
    if (!body.empty() && body.back() == ']') {
        const auto lb = body.rfind(" [");
        if (lb != std::string::npos) {
            p.threshold = body.substr(lb + 2, body.size() - lb - 3);
            body = body.substr(0, lb);
        }
    }
    p.value = body;
    const auto at = origin.rfind(" @");
    p.source = at == std::string::npos ? origin : origin.substr(0, at);
    if (at != std::string::npos) p.timestamp_ms = std::strtoll(origin.c_str() + at + 2, nullptr, 10);
    p.ok = !p.metric.empty();
    return p;
}

} // namespace flux::observatory
