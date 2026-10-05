# Runtime Bottleneck Observation (Step 8.6 — read-only)

Status: **IN PROGRESS.** Module `jni/bottleneck/BottleneckModel.*` (`flux::bottleneck`, NDK `FluxBottleneck`,
host `flux_bottleneck`). Linked into fluxd; **no call path** yet, because no runtime sampler feeds it (see
"Inputs"). Device: NOT_TESTED.

Purpose: let Zairenkai *understand* what may be limiting a game. It never acts: no optimisation, no
policy engine, and no change to GPU, thermal or performance profiles. There is no WebUI.

## Categories and states

Categories: **CPU, GPU, Thermal, Memory, Storage, Display**, or **Unknown** as the primary when
nothing qualifies.

| State | Meaning | Confidence |
|---|---|---|
| CONFIRMED | sustained evidence, a measured frame deficit, and the competing resource measured as not saturated | HIGH |
| LIKELY | majority evidence with a measured frame deficit | MEDIUM |
| POSSIBLE | some evidence, but too few samples, a minority share, or no deficit shown | LOW |
| UNKNOWN | not measured, or measured and not observed | UNKNOWN |

Missing data never raises a state. Every non-Unknown observation carries **evidence** (metric, value,
threshold, source, timestamp), **confidence**, **source** and a **timestamp**.

## Inputs

| Input | Type | Used for |
|---|---|---|
| Session runtime data | `SessionData { package, RuntimeSample[] }` (fps, target Hz, busiest-core utilisation and frequency ratio, GPU busy and frequency ratio, memory/IO PSI, available memory; every field optional) | all categories |
| CapabilityContext | `display.refresh.current_hz`, `display.refresh.max_hz`, `graphics.gpu.load_interface` | target refresh, display-bound check, reason for an unmeasured GPU |
| Performance state | `PerformanceState { profile, lite_mode, refresh_request_hz }` | recorded as evidence only |
| Future Synrei thermal context | `ThermalContext::at(ts) → ThermalState { throttling, cap_ratio, source }` | thermal category. Without it, thermal is UNKNOWN |

`fps` means frames actually presented (SessionRecorder's measurement). It is compared against a target
refresh, and it is never the same thing as display refresh capability (`DISPLAY_INTELLIGENCE.md`).

No sampler exists yet. SessionRecorder measures FPS and CPU temperature, but not CPU/GPU utilisation or
PSI, and Synrei has no thermal interface in fluxd. Wiring a read-only sampler and a Synrei adapter is
future work (B-33).

## Rules (thresholds in `BottleneckModel.hpp`)

- **Frame deficit:** `fps < target × 0.90`. The target is the sample's `target_hz`, otherwise
  `display.refresh.current_hz`.
- **CPU:** busiest core ≥ 0.90 **with** frequency ratio ≥ 0.95. Busy but below max frequency is not a
  CPU limit, because the governor still has headroom. Exclusion for CONFIRMED: GPU busy measured below
  0.70.
- **GPU:** busy ≥ 0.90 (and at max frequency when that is measured). Exclusion: busiest CPU core measured
  below 0.70. If the GPU was not measured and the context says there is no load interface, the note
  says so.
- **Thermal:** the thermal context reports throttling or a cap ratio below 0.90. A thermal limit is
  treated as a *cause* of CPU/GPU saturation, so it becomes the primary without a conflict. An uncapped
  report is evidence against, and stays UNKNOWN.
- **Memory:** memory PSI (some) ≥ 10 %, or available memory below 300 MB. **Storage:** IO PSI (some) ≥ 20 %.
- **Display:** fps ≥ refresh × 0.97 while CPU and GPU are measured idle. CONFIRMED only when the panel
  offers a higher mode (`max_hz` above `current_hz`) and the evidence is sustained.
- **Ladder:**
  - fewer than 3 samples, or a hit share below 60 % → POSSIBLE;
  - no measured deficit → POSSIBLE;
  - at least 60 % hits and a deficit → LIKELY;
  - at least 5 samples, at least 80 % hits, at least 80 % deficit and a measured exclusion → CONFIRMED.
- **Conflicts:** if two or more non-thermal categories are LIKELY or above, the assessment is marked a
  conflict, the primary is UNKNOWN, and any CONFIRMED is downgraded to LIKELY with a note. Mixed
  samples (half CPU, half GPU) stay POSSIBLE.

## Output

`Assessment { timestamp, samples, observations[6], primary, primary_state, conflict, note }`.
`to_facts()` converts it into context facts in domain `bottleneck` (`bottleneck.<kind>` plus
`bottleneck.primary`). These are information for consumers, not capability claims, and nothing
publishes them yet.

## Tests

`tests/bottleneck_model_test.cpp` covers:
- CPU evidence: confirmed; target met gives POSSIBLE; no exclusion gives LIKELY; frequency headroom
  means no CPU limit;
- GPU evidence: confirmed; too short gives LIKELY; no load interface gives UNKNOWN;
- thermal evidence: confirmed and primary; no interface gives UNKNOWN; target met gives POSSIBLE;
  uncapped gives UNKNOWN;
- conflicting evidence: both LIKELY, primary UNKNOWN; mixed samples;
- insufficient data: none, two samples, no fps, fps only;
- memory, storage and display;
- the evidence contract (evidence, confidence, source and timestamp on every observation) and the
  facts export.

## Inputs from the runtime metrics collector (Step 8.7)

`flux::metrics::to_runtime_sample()` (`RUNTIME_METRICS.md`) fills `RuntimeSample` from real procfs/sysfs
metrics. Only readable metrics are copied, and UNKNOWN stays `nullopt`, so the bottleneck rules above
are unchanged. A metric's own `confidence` and `verified` are not yet used to weight evidence. Still
missing before the model runs on a device: a periodic session sampler that calls the collector, and
the Synrei thermal adapter (B-33).

## Fed by the session sampler (Step 8.8)

`RuntimeMetricsSampler` keeps a bounded window of `RuntimeSample`s per session and calls `assess()` on
demand and at session end (`final_assessment()`). The model is still read-only, and nothing acts on
its result. On devices, `fps` is not supplied yet, so without a measured frame deficit the states stay
at most POSSIBLE (B-33A). Thermal stays UNKNOWN until Synrei is connected (B-33B).
