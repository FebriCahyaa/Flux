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
                                             SamplerConfig config, FpsSource fps, ThermalSource thermal)
    : fs_(fs), context_(context), config_(clamp(config)), fps_(std::move(fps)), thermal_(std::move(thermal)),
      history_(std::max<int64_t>(2 * config_.interval_ms, 5000), config_.window) {}

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
    history_.clear();
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
        if (thermal_) add_thermal(snap, now_ms);
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

void RuntimeMetricsSampler::add_thermal(MetricsSnapshot &snap, int64_t now_ms) {
    namespace th = flux::thermal;
    th::ThermalSnapshot t;
    try {
        t = thermal_(now_ms);
    } catch (const std::exception &e) {
        t = th::ThermalSnapshot{};
        t.timestamp_ms = now_ms;
        t.note = std::string("thermal source failed: ") + e.what();
    } catch (...) {
        t = th::ThermalSnapshot{};
        t.timestamp_ms = now_ms;
        t.note = "thermal source failed";
    }
    history_.add(t);
    auto metric = [&](const std::string &id, const char *unit) {
        Metric m;
        m.id = id;
        m.unit = unit;
        m.timestamp_ms = t.timestamp_ms;
        m.source = t.source;
        m.verified = t.verified;
        return m;
    };
    Metric c = metric("thermal.constraint", "");
    c.text = th::to_string(t.constraint);
    c.readable = t.usable();
    c.confidence = c.readable ? t.confidence : cx::Confidence::None;
    c.note = t.note.empty() ? "Synrei state " + t.state : t.note;
    snap.metrics.push_back(c);
    auto temp = [&](const char *id, const th::TempReading &r) {
        Metric m = metric(id, "celsius");
        m.value = r.celsius;
        m.readable = r.readable;
        m.confidence = r.readable ? t.confidence : cx::Confidence::None;
        m.note = r.note;
        snap.metrics.push_back(m);
    };
    temp("thermal.cpu_temp_c", t.cpu);
    temp("thermal.gpu_temp_c", t.gpu);
    temp("thermal.battery_temp_c", t.battery);
    Metric h = metric("thermal.headroom_c", "celsius");
    h.value = t.headroom_c;
    h.readable = t.headroom_c.has_value();
    h.confidence = h.readable ? t.confidence : cx::Confidence::None;
    if (!h.readable) h.note = "headroom not published by the thermal authority";
    snap.metrics.push_back(h);
    Metric sl = metric("thermal.slope_c_per_min", "celsius_per_min");
    sl.value = t.slope_c_per_min;
    sl.readable = t.slope_c_per_min.has_value();
    sl.confidence = sl.readable ? cx::Confidence::Medium : cx::Confidence::None;
    sl.note = sl.readable ? "derived from consecutive verified CPU temperatures" : "needs two verified readings";
    snap.metrics.push_back(sl);
}

flux::bottleneck::Assessment RuntimeMetricsSampler::assess(int64_t now_ms, const flux::bottleneck::PerformanceState &perf,
                                                           const flux::bottleneck::ThermalContext *thermal) const {
    if (state_ == State::Idle && final_) return *final_;
    flux::bottleneck::BottleneckInputs in;
    in.context = context_;
    in.session.package = session_id_;
    in.session.samples.assign(window_.begin(), window_.end());
    in.performance = perf;
    in.thermal = thermal ? thermal : (thermal_ ? &history_ : nullptr);
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
