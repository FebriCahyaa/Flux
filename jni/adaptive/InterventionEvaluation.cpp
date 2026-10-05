#include "InterventionEvaluation.hpp"

#include <algorithm>
#include <cstdio>

namespace zairenkai::adaptive {

namespace b = flux::bottleneck;

const char *to_string(Thermal t) {
    switch (t) {
    case Thermal::Unknown: return "unknown";
    case Thermal::Safety: return "safety";
    case Thermal::Boost: return "boost";
    case Thermal::Other: return "other";
    }
    return "unknown";
}

Thermal from_synrei(const std::optional<flux::thermal::ThermalSnapshot> &s) {
    if (!s || !s->readable || !s->verified) return Thermal::Unknown;
    if (s->state == "safety") return Thermal::Safety;
    if (s->state == "boost") return Thermal::Boost;
    return s->state.empty() ? Thermal::Unknown : Thermal::Other;
}

const char *to_string(Verdict v) {
    switch (v) {
    case Verdict::Observe: return "observe";
    case Verdict::Keep: return "keep";
    case Verdict::Rollback: return "rollback";
    }
    return "observe";
}

namespace {

struct Mean {
    double sum = 0;
    size_t n = 0;
    void add(const std::optional<double> &v) {
        if (v) sum += *v, ++n;
    }
    std::optional<double> get() const { return n ? std::optional<double>(sum / static_cast<double>(n)) : std::nullopt; }
};

/// Median (lower middle for even counts): one spike or one dip does not move the result.
std::optional<double> median(std::vector<double> v) {
    if (v.empty()) return std::nullopt;
    std::sort(v.begin(), v.end());
    return v[(v.size() - 1) / 2];
}

std::string fmt(const char *name, const std::optional<double> &v) {
    if (!v) return std::string(name) + "=unknown";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s=%.3f", name, *v);
    return buf;
}

} // namespace

WindowSummary summarize(const std::vector<Sample> &w) {
    WindowSummary s;
    s.samples = w.size();
    std::vector<double> fps, shortfall;
    Mean cpu, gpu, cf, gf;
    for (const auto &x : w) {
        if (x.fps && x.target_hz && *x.target_hz > 0) {
            ++s.fps_samples;
            fps.push_back(*x.fps);
            shortfall.push_back(std::max(0.0, *x.target_hz - *x.fps) / *x.target_hz);
        }
        cpu.add(x.cpu_busy);
        gpu.add(x.gpu_busy);
        cf.add(x.cpu_freq_ratio);
        gf.add(x.gpu_freq_ratio);
        if (x.thermal != Thermal::Unknown) ++s.thermal_known;
        if (x.thermal == Thermal::Safety) ++s.safety;
    }
    s.fps = median(fps);
    s.shortfall = median(shortfall);
    s.cpu_busy = cpu.get();
    s.gpu_busy = gpu.get();
    s.cpu_freq_ratio = cf.get();
    s.gpu_freq_ratio = gf.get();
    return s;
}

Evaluation evaluate(const std::vector<Sample> &baseline, const std::vector<Sample> &post, const std::string &target,
                    bool mitigation) {
    Evaluation e;
    e.baseline = summarize(baseline);
    e.post = summarize(post);
    const auto &B = e.baseline, &P = e.post;
    e.evidence = {fmt("baseline_fps", B.fps), fmt("post_fps", P.fps), fmt("baseline_shortfall", B.shortfall),
                  fmt("post_shortfall", P.shortfall), "post_samples=" + std::to_string(P.samples),
                  "post_safety_samples=" + std::to_string(P.safety)};
    auto done = [&](Verdict v, const std::string &reason) {
        e.verdict = v;
        e.reason = reason;
        return e;
    };
    const bool window_full = P.samples >= kMaxPostSamples;

    if (mitigation) {
        // MITIGATE relieves thermal load: it is kept while Synrei reports safety or the window is
        // not yet conclusive; performance numbers never roll it back.
        if (P.samples < kMinPostFps) return done(Verdict::Observe, "insufficient_post_evidence");
        return done(Verdict::Keep, P.safety ? "mitigation_while_thermal_safety" : "mitigation_thermal_relief");
    }

    // 1. Sustainability first: Synrei safety after a performance intervention is an unacceptable cost.
    if (P.safety > 0) return done(Verdict::Rollback, "thermal_safety_after_intervention");

    // 2. Enough evidence?
    if (B.fps_samples < kMinBaselineFps) return done(window_full ? Verdict::Rollback : Verdict::Observe, "insufficient_baseline");
    if (P.fps_samples < kMinPostFps)
        return done(window_full ? Verdict::Rollback : Verdict::Observe, window_full ? "no_fps_evidence" : "insufficient_post_evidence");

    // 3. Bottleneck relationship: judged on the target outcome (frame delivery), not on whether the
    //    target resource is still the limit. A shift to another resource is recorded, not a failure.
    const b::Kind want = target == "gpu" ? b::Kind::Gpu : b::Kind::Cpu;
    if (!post.empty()) {
        const auto &last = post.back();
        e.bottleneck_shifted = !last.bottleneck_conflict && last.bottleneck != b::Kind::Unknown &&
                               last.bottleneck != want && last.bottleneck_state >= b::State::Likely;
        if (e.bottleneck_shifted) e.evidence.push_back(std::string("bottleneck_shifted_to=") + b::to_string(last.bottleneck));
    }

    // 4. Benefit: reduced shortfall or higher mean FPS, never a clock or utilisation change alone.
    const bool shortfall_gain = *B.shortfall - *P.shortfall >= kMinShortfallGain;
    const bool fps_gain = *B.fps > 0 && (*P.fps - *B.fps) / *B.fps >= kMinFpsGain;
    const bool benefit = shortfall_gain || fps_gain;

    // 5. Thermal evidence must still be current to call the result sustainable.
    if (P.thermal_known == 0)
        return done(window_full ? Verdict::Rollback : Verdict::Observe, window_full ? "thermal_evidence_lost" : "awaiting_thermal_evidence");

    if (benefit) return done(Verdict::Keep, e.bottleneck_shifted ? "benefit_bottleneck_shifted" : "benefit");

    // 6. Cost without benefit: sustained higher load on the target resource.
    const auto &bl = want == b::Kind::Gpu ? B.gpu_busy : B.cpu_busy;
    const auto &pl = want == b::Kind::Gpu ? P.gpu_busy : P.cpu_busy;
    if (bl && pl && *pl - *bl >= kCostLoadRise) return done(Verdict::Rollback, "cost_without_benefit");
    return done(window_full ? Verdict::Rollback : Verdict::Observe, window_full ? "no_benefit" : "benefit_not_yet_shown");
}

} // namespace zairenkai::adaptive
