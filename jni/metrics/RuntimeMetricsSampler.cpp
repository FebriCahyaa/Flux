#include "RuntimeMetricsSampler.hpp"

#include <algorithm>
#include <exception>

namespace flux::metrics {

namespace cx = flux::context;

SamplerConfig clamp(SamplerConfig c) {
    c.interval_ms = std::clamp(c.interval_ms, kMinIntervalMs, kMaxIntervalMs);
    c.window = std::clamp(c.window, kMinWindow, kMaxWindow);
    c.fps_max_age_ms = std::clamp(c.fps_max_age_ms, kMinFpsAgeMs, kMaxFpsAgeMs);
    return c;
}

RuntimeMetricsSampler::RuntimeMetricsSampler(const flux::kernel::ReadOnlyFs &fs, const cx::CapabilityContext *context,
                                             SamplerConfig config, FpsSource fps)
    : fs_(fs), context_(context), config_(clamp(config)), fps_(std::move(fps)) {}

bool RuntimeMetricsSampler::start(const std::string &session_id, int64_t now_ms) {
    if (state_ == State::Running && session_id == session_id_) return false; // duplicate
    if (state_ == State::Running) stop(now_ms);                             // replaced by another session
    session_id_ = session_id;
    collector_ = std::make_unique<RuntimeMetricsCollector>(fs_, context_); // fresh deltas per session
    window_.clear();
    last_.reset();
    final_.reset();
    taken_ = 0;
    failures_ = 0;
    last_fps_ts_ = 0;
    state_ = State::Running;
    last_sample_ms_ = now_ms - config_.interval_ms; // sample immediately
    tick(now_ms);
    return true;
}

void RuntimeMetricsSampler::stop(int64_t now_ms) {
    if (state_ == State::Idle) return;
    try {
        final_ = assess(now_ms);
    } catch (...) {
        final_.reset();
    }
    collector_.reset();
    state_ = State::Idle;
}

void RuntimeMetricsSampler::tick(int64_t now_ms) {
    if (state_ != State::Running || !collector_) return;
    if (now_ms - last_sample_ms_ < config_.interval_ms) return;
    last_sample_ms_ = now_ms;

    SampleNotice n;
    n.session_id = session_id_;
    n.timestamp_ms = now_ms;
    try {
        auto snap = collector_->sample(now_ms);
        // FPS: SessionRecorder's latest observation, accepted only when valid, fresh and new.
        FpsAcceptance fa{std::nullopt, "no FPS source connected"};
        std::optional<FpsObservation> obs;
        if (fps_) {
            obs = fps_();
            fa = accept_fps(obs, now_ms, config_.fps_max_age_ms, last_fps_ts_);
        }
        Metric fm;
        fm.id = "fps";
        fm.unit = "fps";
        fm.timestamp_ms = obs ? obs->timestamp_ms : now_ms;
        fm.note = fa.reason;
        if (fa.fps) {
            last_fps_ts_ = obs->timestamp_ms;
            fm.value = fa.fps;
            fm.source = "session_recorder:" + obs->source;
            fm.confidence = cx::Confidence::High;
            fm.readable = true;
        }
        snap.metrics.push_back(fm);
        const std::optional<double> fps = fa.fps;
        std::optional<double> target;
        if (context_) {
            auto r = context_->resolve("display.refresh.current_hz");
            if (r.support == cx::Support::Yes && r.fact && !r.fact->value.empty()) {
                try {
                    target = std::stod(r.fact->value);
                } catch (...) {
                }
            }
        }
        window_.push_back(to_runtime_sample(snap, fps, target));
        while (window_.size() > config_.window) window_.pop_front();
        for (const auto &m : snap.metrics) (m.readable ? n.readable : n.unknown)++;
        last_ = std::move(snap);
        failures_ = 0;
        n.index = ++taken_;
    } catch (const std::exception &e) {
        n.failed = true;
        n.error = e.what();
    } catch (...) {
        n.failed = true;
        n.error = "unknown collector failure";
    }
    if (n.failed && ++failures_ >= kMaxConsecutiveFailures) state_ = State::Failed; // no runaway retries
    notify(n);
}

flux::bottleneck::Assessment RuntimeMetricsSampler::assess(int64_t now_ms, const flux::bottleneck::PerformanceState &perf,
                                                           const flux::bottleneck::ThermalContext *thermal) const {
    if (state_ == State::Idle && final_) return *final_;
    flux::bottleneck::BottleneckInputs in;
    in.context = context_;
    in.session.package = session_id_;
    in.session.samples.assign(window_.begin(), window_.end());
    in.performance = perf;
    in.thermal = thermal;
    in.now_ms = now_ms;
    return flux::bottleneck::assess(in);
}

void RuntimeMetricsSampler::notify(const SampleNotice &n) const {
    if (!observer_) return;
    try {
        observer_(n);
    } catch (...) {
        // Observatory problems never affect sampling.
    }
}

void SamplerParticipant::begin(const flux::session::SessionInfo &s) {
    try {
        sampler_.start(s.id, s.started_ms);
    } catch (...) {
        // Sampling is optional; the session continues without it.
    }
}

bool SamplerParticipant::end(const flux::session::SessionInfo &s, flux::session::EndReason) {
    try {
        sampler_.stop(s.ended_ms ? s.ended_ms : s.started_ms);
    } catch (...) {
    }
    return true; // nothing to restore: sampling never changed the device
}

void SamplerParticipant::tick(const flux::session::SessionInfo &, int64_t now_ms) {
    try {
        sampler_.tick(now_ms);
    } catch (...) {
    }
}

} // namespace flux::metrics
