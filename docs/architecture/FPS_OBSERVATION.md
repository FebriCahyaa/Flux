# FPS Observation Bridge (Step 8.8.1 — read-only)

Status: **IN PROGRESS.** `jni/metrics/FpsObservation.*`, with the publish hook in `jni/SessionRecorder.*` and
the consumer in `RuntimeMetricsSampler`. Device: NOT_TESTED.

## Ownership

| Component | Owns |
|---|---|
| SessionRecorder | **the only FPS measurement** (fpsgo → game layer → SDE display → SurfaceFlinger page flips), unchanged |
| CapabilityContext | the display target refresh capability (`display.refresh.current_hz`) |
| RuntimeMetricsSampler | reading the latest observation once per sample and accepting or rejecting it |
| BottleneckModel | consuming FPS + target refresh + runtime metrics; it measures nothing |

There is no second FPS collector and no new loop: the sampler reads the slot only when it takes a
sample (tested), and SessionRecorder's own 1 s loop is unchanged.

## Interface

`FpsObservation { timestamp_ms, fps, valid, source }`, held in an `FpsObservationSlot` that keeps only
the latest value behind a mutex:
- SessionRecorder publishes right after its existing `samples_.push_back` / `write_live`, with the same
  `fps` and `source` it already computed. A second has no reading when `fps` is NaN, which gives
  `valid=false`.
- The timestamp uses the **steady** clock, the same clock as the session manager and the sampler. The
  recorder's own JSON timestamps stay on the wall clock, unchanged.
- `stop()` clears the slot, so a finished session's FPS is never reused.
- Readers get it through `SessionRecorder::fps_observation().latest()`, as a copy.

Unchanged: `sessions.json`, `session_live.json`, the FPS sources and their order, `RefreshMatcher`,
and the start/stop lifecycle.

## Acceptance (`accept_fps`)

An observation becomes the sample's `fps` only when all of these hold:
1. it exists ("no observation from SessionRecorder");
2. it is valid ("SessionRecorder had no frame-rate reading");
3. it is not in the future;
4. it is not older than `fps_max_age_ms` (default 3000, clamped 1000–10000): "stale";
5. it is **newer** than the observation used for the previous sample. The same reading is never
   counted twice.

Otherwise FPS is **UNKNOWN** for that sample. It is never estimated from CPU or GPU load.

The sampler records the result as a `fps` metric in its snapshot. That metric carries the value, the
observation's own timestamp, source `session_recorder:<source>`, HIGH confidence when accepted,
`readable`, `verified=false`, and the reason in `note` when rejected.

## Bottleneck input

Each `RuntimeSample` combines:
- `fps` from the accepted observation;
- `target_hz` from the CapabilityContext;
- the runtime metrics.

The evidence rules and thresholds are **unchanged**. A frame shortfall needs actual FPS: without it,
states stay at most POSSIBLE (tested on the same CPU-bound series with and without FPS).

## Tests

`tests/fps_observation_test.cpp` covers:
- acceptance: available, missing, invalid, stale, the boundary, future, repeated and older observations;
- the slot: NaN and zero are invalid, clear works, and a writer thread against a reader sees no torn
  values;
- the sampler: no observation, accepted (source and timestamp), the same reading not reused, stale;
- target refresh integration;
- the bottleneck receiving FPS: CONFIRMED with FPS, POSSIBLE without, and FPS never estimated;
- no second loop: FPS reads equal samples taken, and none after stop.

SessionRecorder has no host test harness (it needs spdlog and device `/sys`). Its change is additive:
one publish after the existing write, a `clear()` in `stop()`, and a `const` accessor. It is checked
by diff review, a header compile check and the CI NDK build.
