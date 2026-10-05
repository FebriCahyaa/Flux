#include "BottleneckResult.hpp"

namespace flux::bottleneck {

namespace {

Finding finding_of(const Observation &o) {
    return {o.kind, o.state, o.confidence, o.source, o.timestamp_ms, o.evidence, o.note};
}

} // namespace

BottleneckResult make_result(const Assessment &a, const std::string &session_id) {
    BottleneckResult r;
    r.session_id = session_id;
    r.timestamp_ms = a.timestamp_ms;
    r.samples = a.samples;
    r.conflict = a.conflict;
    r.note = a.note;
    const Observation *primary = a.primary == Kind::Unknown ? nullptr : a.get(a.primary);
    if (primary) {
        r.primary = finding_of(*primary);
    } else {
        r.primary.kind = Kind::Unknown; // insufficient evidence or conflict: nothing invented
        r.primary.note = a.note;
        r.primary.timestamp_ms = a.timestamp_ms;
    }
    for (const auto &o : a.observations)
        if (o.state >= State::Possible && (!primary || o.kind != primary->kind)) r.secondary.push_back(finding_of(o));
    return r;
}

} // namespace flux::bottleneck
