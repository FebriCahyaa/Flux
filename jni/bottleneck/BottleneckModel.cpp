#include "BottleneckModel.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>

namespace flux::bottleneck {

namespace cx = flux::context;

const char *to_string(Kind k) {
    switch (k) {
    case Kind::Cpu: return "cpu";
    case Kind::Gpu: return "gpu";
    case Kind::Thermal: return "thermal";
    case Kind::Memory: return "memory";
    case Kind::Storage: return "storage";
    case Kind::Display: return "display";
    case Kind::Unknown: break;
    }
    return "unknown";
}

const char *to_string(State s) {
    switch (s) {
    case State::Possible: return "possible";
    case State::Likely: return "likely";
    case State::Confirmed: return "confirmed";
    case State::Unknown: break;
    }
    return "unknown";
}

const Observation *Assessment::get(Kind k) const {
    for (const auto &o : observations)
        if (o.kind == k) return &o;
    return nullptr;
}

namespace {

cx::Confidence confidence_of(State s) {
    switch (s) {
    case State::Confirmed: return cx::Confidence::High;
    case State::Likely: return cx::Confidence::Medium;
    case State::Possible: return cx::Confidence::Low;
    case State::Unknown: break;
    }
    return cx::Confidence::None;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::optional<double> context_number(const cx::CapabilityContext *c, const char *id) {
    if (!c) return std::nullopt;
    auto r = c->resolve(id);
    if (r.support != cx::Support::Yes || !r.fact || r.fact->value.empty()) return std::nullopt;
    try {
        return std::stod(r.fact->value);
    } catch (...) {
        return std::nullopt;
    }
}

/// Per-kind tally over the samples.
struct Tally {
    size_t measured = 0, hits = 0;
    double peak = 0;
    int64_t last_hit_ms = 0;
};

using Condition = std::function<std::optional<std::pair<bool, double>>(const RuntimeSample &)>;

Tally tally(const std::vector<RuntimeSample> &samples, const Condition &cond) {
    Tally t;
    for (const auto &s : samples) {
        auto r = cond(s);
        if (!r) continue;
        ++t.measured;
        t.peak = std::max(t.peak, r->second);
        if (r->first) {
            ++t.hits;
            t.last_hit_ms = s.timestamp_ms;
        }
    }
    return t;
}

double share(const Tally &t) { return t.measured ? double(t.hits) / double(t.measured) : 0.0; }

struct Frames {
    Tally deficit; // fps < target * kDeficit
    bool known() const { return deficit.measured > 0; }
};

State ladder(const Tally &t, size_t total, const Frames &frames, bool exclusion) {
    if (t.measured == 0 || t.hits == 0) return State::Unknown;
    if (total < kMinSamples || share(t) < kMajority) return State::Possible;
    if (!frames.known() || share(frames.deficit) < kMajority) return State::Possible;
    if (t.measured >= kConfirmSamples && share(t) >= kSustained && share(frames.deficit) >= kSustained && exclusion)
        return State::Confirmed;
    return State::Likely;
}

Evidence tally_evidence(const char *metric, const Tally &t, const std::string &threshold, const std::string &source,
                        int64_t fallback_ms) {
    return {metric, "peak " + num(t.peak) + ", " + std::to_string(t.hits) + "/" + std::to_string(t.measured) + " samples",
            threshold, source, t.last_hit_ms ? t.last_hit_ms : fallback_ms};
}

/// Share of samples where `metric` was measured and below kIdle (a measured exclusion).
bool excluded(const std::vector<RuntimeSample> &samples, std::optional<double> RuntimeSample::*metric) {
    size_t measured = 0, idle = 0;
    for (const auto &s : samples) {
        if (!(s.*metric)) continue;
        ++measured;
        if (*(s.*metric) < kIdle) ++idle;
    }
    return measured >= kConfirmSamples && double(idle) / double(measured) >= kSustained;
}

} // namespace

Assessment assess(const BottleneckInputs &in) {
    Assessment a;
    const auto &samples = in.session.samples;
    a.samples = samples.size();
    const int64_t last_ms = samples.empty() ? in.now_ms : samples.back().timestamp_ms;
    a.timestamp_ms = in.now_ms ? in.now_ms : last_ms;
    const auto ctx_refresh = context_number(in.context, "display.refresh.current_hz");
    const auto ctx_max = context_number(in.context, "display.refresh.max_hz");

    auto target_of = [&](const RuntimeSample &s) -> std::optional<double> {
        if (s.target_hz && *s.target_hz > 0) return s.target_hz;
        return ctx_refresh;
    };
    Frames frames;
    frames.deficit = tally(samples, [&](const RuntimeSample &s) -> std::optional<std::pair<bool, double>> {
        auto target = target_of(s);
        if (!s.fps || !target) return std::nullopt;
        return std::make_pair(*s.fps < *target * kDeficit, *s.fps);
    });
    const Evidence deficit_ev{"frame_deficit",
                              std::to_string(frames.deficit.hits) + "/" + std::to_string(frames.deficit.measured) +
                                  " samples below target",
                              "fps < target * " + num(kDeficit), "session",
                              frames.deficit.last_hit_ms ? frames.deficit.last_hit_ms : last_ms};
    const Evidence perf_ev{"performance_state",
                           "profile=" + (in.performance.profile.empty() ? std::string("unknown") : in.performance.profile) +
                               (in.performance.lite_mode ? " lite" : "") +
                               " refresh_request=" + std::to_string(in.performance.refresh_request_hz),
                           "", "performance_state", last_ms ? last_ms : in.now_ms};

    auto make = [&](Kind k, State s, const std::string &source, std::vector<Evidence> ev, std::string note) {
        Observation o;
        o.kind = k;
        o.state = s;
        o.confidence = confidence_of(s);
        o.timestamp_ms = last_ms;
        if (s == State::Unknown) {
            o.note = note.empty() ? "no evidence" : std::move(note);
            return o;
        }
        o.source = source;
        o.evidence = std::move(ev);
        if (frames.known()) o.evidence.push_back(deficit_ev);
        o.evidence.push_back(perf_ev);
        o.note = std::move(note);
        return o;
    };
    auto unmeasured = [&](const char *what) {
        return samples.empty() ? std::string("insufficient data: no samples") : std::string(what) + " not measured";
    };

    // CPU: busiest core saturated at (or near) max frequency.
    {
        auto t = tally(samples, [](const RuntimeSample &s) -> std::optional<std::pair<bool, double>> {
            if (!s.cpu_busiest_core) return std::nullopt;
            const bool at_max = !s.cpu_freq_ratio || *s.cpu_freq_ratio >= kAtMaxFreq;
            return std::make_pair(*s.cpu_busiest_core >= kSaturated && at_max && s.cpu_freq_ratio.has_value(),
                                  *s.cpu_busiest_core);
        });
        const bool excl = excluded(samples, &RuntimeSample::gpu_busy);
        auto st = ladder(t, samples.size(), frames, excl);
        std::vector<Evidence> ev{tally_evidence("cpu_busiest_core", t, ">= " + num(kSaturated) + " at freq >= " + num(kAtMaxFreq), "session", last_ms)};
        if (excl) ev.push_back({"gpu_busy", "below " + num(kIdle) + " in most samples", "exclusion", "session", last_ms});
        a.observations.push_back(make(Kind::Cpu, st, "session", ev,
                                      t.measured == 0 ? unmeasured("cpu utilisation/frequency")
                                      : st == State::Unknown ? "measured; never saturated at max frequency" : ""));
    }
    // GPU.
    {
        auto t = tally(samples, [](const RuntimeSample &s) -> std::optional<std::pair<bool, double>> {
            if (!s.gpu_busy) return std::nullopt;
            const bool at_max = !s.gpu_freq_ratio || *s.gpu_freq_ratio >= kAtMaxFreq;
            return std::make_pair(*s.gpu_busy >= kSaturated && at_max, *s.gpu_busy);
        });
        const bool excl = excluded(samples, &RuntimeSample::cpu_busiest_core);
        auto st = ladder(t, samples.size(), frames, excl);
        std::string note;
        if (t.measured == 0) {
            const bool no_iface = in.context && in.context->supports("graphics.gpu.load_interface") == cx::Support::No;
            note = no_iface ? "gpu_busy not measured (no GPU load interface on this device)" : unmeasured("gpu_busy");
        } else if (st == State::Unknown) {
            note = "measured; never saturated";
        }
        std::vector<Evidence> ev{tally_evidence("gpu_busy", t, ">= " + num(kSaturated), "session", last_ms)};
        if (excl) ev.push_back({"cpu_busiest_core", "below " + num(kIdle) + " in most samples", "exclusion", "session", last_ms});
        a.observations.push_back(make(Kind::Gpu, st, "session", ev, note));
    }
    // Thermal: from the (future Synrei) thermal context only.
    {
        std::string source = "synrei";
        State st = State::Unknown;
        std::string note;
        std::vector<Evidence> ev;
        if (!in.thermal) {
            note = "no thermal context (Synrei interface not connected)";
        } else {
            auto t = tally(samples, [&](const RuntimeSample &s) -> std::optional<std::pair<bool, double>> {
                auto th = in.thermal->at(s.timestamp_ms);
                if (!th) return std::nullopt;
                if (!th->source.empty()) source = th->source;
                const double cap = th->cap_ratio.value_or(1.0);
                return std::make_pair(th->throttling || cap < kThermalCap, 1.0 - cap);
            });
            st = ladder(t, samples.size(), frames, true);
            ev.push_back(tally_evidence("thermal_cap", t, "throttling or cap < " + num(kThermalCap), source, last_ms));
            note = t.measured == 0 ? unmeasured("thermal state") : st == State::Unknown ? "measured; no throttling" : "";
        }
        a.observations.push_back(make(Kind::Thermal, st, source, ev, note));
    }
    // Memory and storage pressure.
    auto pressure = [&](Kind k, const char *metric, std::optional<double> RuntimeSample::*field, double limit) {
        auto t = tally(samples, [&](const RuntimeSample &s) -> std::optional<std::pair<bool, double>> {
            bool low_mem = k == Kind::Memory && s.mem_available_mb && *s.mem_available_mb < kMemLowMb;
            if (!(s.*field) && !low_mem) return std::nullopt;
            double v = (s.*field).value_or(0);
            return std::make_pair(v >= limit || low_mem, v);
        });
        auto st = ladder(t, samples.size(), frames, true);
        std::vector<Evidence> ev{tally_evidence(metric, t, ">= " + num(limit) + "%", "session", last_ms)};
        a.observations.push_back(make(k, st, "session", ev,
                                      t.measured == 0 ? unmeasured(metric) : st == State::Unknown ? "measured; no pressure" : ""));
    };
    pressure(Kind::Memory, "mem_psi_some", &RuntimeSample::mem_psi_some, kMemPsi);
    pressure(Kind::Storage, "io_psi_some", &RuntimeSample::io_psi_some, kIoPsi);
    // Display: frames pinned to the panel refresh while CPU and GPU are measured idle.
    {
        auto t = tally(samples, [&](const RuntimeSample &s) -> std::optional<std::pair<bool, double>> {
            auto refresh = ctx_refresh ? ctx_refresh : s.target_hz;
            if (!s.fps || !refresh || *refresh <= 0) return std::nullopt;
            return std::make_pair(*s.fps >= *refresh * kAtRefresh, *s.fps);
        });
        const bool excl = excluded(samples, &RuntimeSample::cpu_busiest_core) && excluded(samples, &RuntimeSample::gpu_busy);
        State st = State::Unknown;
        std::string note;
        if (t.measured && t.hits) {
            st = State::Possible;
            if (samples.size() >= kMinSamples && share(t) >= kMajority && excl) {
                const bool higher = ctx_refresh && ctx_max && *ctx_max > *ctx_refresh;
                st = higher && t.measured >= kConfirmSamples && share(t) >= kSustained ? State::Confirmed : State::Likely;
                note = higher ? "frames pinned to the current refresh; the panel supports a higher mode"
                              : "frames pinned to the refresh rate (higher mode not known)";
            } else if (!excl) {
                note = "frames at refresh, but CPU/GPU headroom not measured as idle";
            }
        } else {
            note = t.measured == 0 ? unmeasured("fps / refresh") : "measured; frames below the refresh rate";
        }
        std::vector<Evidence> ev{tally_evidence("fps_vs_refresh", t, "fps >= refresh * " + num(kAtRefresh), "session", last_ms)};
        if (ctx_refresh)
            ev.push_back({"display.refresh.current_hz", num(*ctx_refresh), "", "capability_context", last_ms ? last_ms : in.now_ms});
        if (ctx_max) ev.push_back({"display.refresh.max_hz", num(*ctx_max), "", "capability_context", last_ms ? last_ms : in.now_ms});
        a.observations.push_back(make(Kind::Display, st, "session", ev, note));
    }

    // Primary: thermal explains CPU/GPU saturation; otherwise one clear candidate or a conflict.
    std::vector<Observation *> strong;
    Observation *thermal = nullptr;
    for (auto &o : a.observations) {
        if (o.state < State::Likely) continue;
        if (o.kind == Kind::Thermal) thermal = &o;
        else strong.push_back(&o);
    }
    if (samples.empty()) {
        a.note = "insufficient data: no session samples";
    } else if (thermal) {
        a.primary = Kind::Thermal;
        a.primary_state = thermal->state;
        a.note = strong.empty() ? "thermal limit" : "thermal limit explains other saturation";
    } else if (strong.size() == 1) {
        a.primary = strong[0]->kind;
        a.primary_state = strong[0]->state;
    } else if (strong.size() > 1) {
        a.conflict = true;
        a.note = "conflict:";
        for (auto *o : strong) {
            a.note += std::string(" ") + to_string(o->kind);
            if (o->state == State::Confirmed) {
                o->state = State::Likely;
                o->confidence = confidence_of(State::Likely);
                o->note += (o->note.empty() ? "" : "; ") + std::string("downgraded: competing evidence");
            }
        }
    } else {
        a.note = samples.size() < kMinSamples ? "insufficient data: fewer than 3 samples" : "no constraint with sufficient evidence";
    }
    return a;
}

std::vector<cx::CapabilityFact> to_facts(const Assessment &a) {
    std::vector<cx::CapabilityFact> out;
    auto fact = [&](const std::string &id, State s, const std::string &value, const std::string &source, const std::string &note) {
        cx::CapabilityFact f;
        f.id = id;
        f.domain = "bottleneck";
        f.source = source;
        f.support = s == State::Unknown ? cx::Support::Unknown : cx::Support::Yes;
        f.readable = s != State::Unknown;
        f.confidence = confidence_of(s);
        f.risk = cx::Risk::Unknown;
        f.value = value;
        f.note = note;
        out.push_back(std::move(f));
    };
    for (const auto &o : a.observations)
        fact(std::string("bottleneck.") + to_string(o.kind), o.state, to_string(o.state), o.source, o.note);
    fact("bottleneck.primary", a.primary_state, to_string(a.primary), "assessment", a.note);
    return out;
}

} // namespace flux::bottleneck
