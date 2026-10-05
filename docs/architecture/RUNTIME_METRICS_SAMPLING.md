# Runtime Metrics Sampling Lifecycle (Step 8.8 — read-only)

Status: **IN PROGRESS.** `jni/metrics/RuntimeMetricsSampler.*` (`flux::metrics::RuntimeMetricsSampler`,
`SamplerParticipant`). It is registered in fluxd's SessionManager (`jni/SessionHost.cpp`).
Device: NOT_TESTED.

## Ownership

```
SessionManager ── owns the sampler lifecycle only (SamplerParticipant: begin → start, end → stop, tick → tick)
   └─ RuntimeMetricsSampler ── schedules, collects (one RuntimeMetricsCollector per session),
         keeps the previous sample, timestamps, keeps a bounded window
            └─ BottleneckModel ── judges evidence only (assess / final_assessment)
```

No component applies policy. Sampling never touches governors, clocks, ZRAM, the I/O scheduler,
refresh or thermal state. The collector only has `ReadOnlyFs`, which has no write operation.

## Lifecycle

| Event | Sampler |
|---|---|
| Session begins (`SessionManager::begin`) | `start(session_id)`: a fresh collector and the first sample taken immediately |
| Duplicate begin / same session | ignored (`start` returns false; no second collector) |
| Session switch | the old session is stopped (final assessment kept), then the new one starts from a fresh collector |
| Session tick (only while a session is active) | samples when `interval_ms` has elapsed |
| Exit, focus loss, process death, failure, switch, daemon stop (`SessionManager::end`) | `stop()`; idempotent; the final assessment is kept |
| After stop | ticks do nothing; `needs_tick()` is false |

The sampler is registered **last**, so it begins after the game runtime and the recorder and ends first.
Sampling stops before any restore runs.

## Bounds

- **No thread or worker.** Sampling runs on the daemon's existing session tick. While a session is
  active the participant asks for ticks (the main loop already wakes about once per second for
  sessions); outside a session there are no ticks. There is no permanent daemon sampler.
- **Interval:** `SamplerConfig::interval_ms`, default 2000 in fluxd, clamped to 1000–60000 ms.
- **Window:** `SamplerConfig::window`, default 120 samples, clamped to 3–900. Older samples are
  dropped.
- **Single sampler per daemon**, and one collector per session.
- **Failure:** a collector exception skips that sample. After 3 consecutive failures the sampler enters
  `Failed` and stops asking for ticks until the next session. It never throws into the session, the
  participant's `end()` always reports clean, and the game session continues.

## Samples

- **First sample of a session:** delta metrics (CPU utilisation, iowait, IO latency) are UNKNOWN
  ("needs a previous sample"). From the second sample they are measured over the interval.
- Each `Metric` keeps its source, confidence, readable, verified and timestamp (`last_snapshot()`).
- The window holds `RuntimeSample`s from `to_runtime_sample()`. UNKNOWN stays `nullopt`.
- `fps` comes from a `FrameSource` callback. fluxd passes none yet (SessionRecorder has no accessor),
  so the frame deficit is unknown on devices and the bottleneck states top out at POSSIBLE (B-33A).
- The target refresh comes from the CapabilityContext (`display.refresh.current_hz`). It is not probed
  again.

## Capability knowledge

The collector reads interfaces directly, only to take measurements. Capability knowledge such as the I/O
scheduler and the display refresh comes from the CapabilityContext. The sampler never re-runs Kernel,
Graphics or Display intelligence.

## Observatory

`set_observer(SampleNotice)` receives the session id, timestamp, index, readable/unknown counts and
any failure, after each sample. Exceptions are swallowed. It is **not connected**: there is no event
type, no persistence and no telemetry storage.

## Tests

`tests/runtime_metrics_sampler_test.cpp` covers:
- the sampler starting with the session;
- a duplicate start being ignored;
- the interval being respected;
- the sampler stopping on session end, process death, daemon stop, focus loss and failure;
- a switch giving one sampler with a fresh collector;
- the first sample being UNKNOWN for deltas and the second producing load and latency;
- missing metrics staying UNKNOWN;
- a collector failure not ending the session and stopping after 3 failures;
- a throwing observer being swallowed;
- no node being modified, and the interval and window clamps;
- samples being forwarded to the BottleneckModel.
