# Bottleneck Result Integration (Step 8.10 — observation only)

Status: **IN PROGRESS.** These are new:
- `jni/bottleneck/BottleneckResult.*` (the neutral result);
- `jni/bridge/BottleneckEvents.*` (Observatory events);
- registry types in `jni/observatory/Event.cpp`;
- the sampler's result sink in `jni/metrics/RuntimeMetricsSampler.*`;
- fluxd wiring in `jni/SessionHost.cpp`.

Device: NOT_TESTED.

Purpose: make the final assessment observable and consumable. **No policy reads it**, and nothing is
optimised, retuned or changed because of it.

## Flow

```
Runtime metrics + FPS observation + display capability + Synrei thermal context
        │ (RuntimeMetricsSampler window, per session)
        ▼
BottleneckModel::assess  ── once, at session end (SamplerParticipant::end → sampler.stop)
        ▼
make_result → BottleneckResult ──▶ result sink ──▶ BOTTLENECK_ASSESSED (Observatory EventSink)
        └─ assessment threw  ──────▶ failure sink ──▶ BOTTLENECK_ANALYSIS_FAILED
```

There is no continuous bottleneck worker. The assessment runs once per session, when the sampler
stops. Session end, process death, focus loss, failure, switch and daemon stop all trigger it, and a
duplicate stop does nothing. The event is emitted only after `assess()` returned for the complete
window.

## BottleneckResult

| Field | Meaning |
|---|---|
| `primary` (`Finding`) | kind, rating (CONFIRMED / LIKELY / POSSIBLE / UNKNOWN), confidence, source, timestamp, evidence, note. **`Unknown` when no finding has sufficient evidence or the evidence conflicts**; nothing is invented |
| `secondary` | every other category rated POSSIBLE or higher, with its evidence (in a conflict, the competing categories) |
| `conflict` | the assessment's conflict flag |
| `samples`, `timestamp_ms`, `note`, `session_id` | from the assessment |

`make_result` copies; it never re-rates. Rules and thresholds are unchanged (`BOTTLENECK_MODEL.md`).

## Events (category `performance`, source `bottleneck`, session required)

| Type | When | Content |
|---|---|---|
| `BOTTLENECK_ASSESSED` | the final assessment completed | severity info, result `ok`, confidence = primary's. `after` holds `primary`, `rating`, `confidence`, `conflict`, `samples`, `assessed_at_ms`, `note`, `secondary` (`kind:rating,…`) and `evidence.<kind>.<n>` = `metric=value [threshold] (source @ts)`, at most 8 per finding. `reason` is `"<kind> <rating>"` or `"no finding with sufficient evidence: …"` |
| `BOTTLENECK_ANALYSIS_FAILED` | the assessment or the result construction threw | severity warning, result `failed`, `reason` = the error. No finding is claimed |

Events respect the Observatory limits (64 keys, 512-character values and reason). In fluxd they go to
the existing bounded in-memory store (`flux_observatory::store()`). There is no persistence.

## Failure isolation

`SamplerParticipant` is the last participant registered, so it ends **first**. All analysis and
callbacks run inside `try/catch`, and `end()` always reports clean. When the analysis fails:
- the game session ends normally;
- the game runtime participant still restores (tested with a stand-in);
- no transaction is touched, because the sampler has no access to one;
- a failure event is recorded when the sink works.

Throwing sinks (Observatory problems) are swallowed.

## Not changed

`sessions.json`, `session_live.json`, SessionRecorder FPS measurement, GameRuntime policy,
PerformancePlanner, Synrei/HiCo, kernel nodes, and the bottleneck thresholds.

## Tests

`tests/bottleneck_result_test.cpp` covers:
- results: CPU, GPU, thermal (with CPU as secondary), memory, storage and display, each confirmed;
- a conflicting result (primary UNKNOWN, both kept as secondary);
- insufficient evidence;
- event contents (validates against the registry, bounded, survives a JSON round trip; the
  insufficient-evidence event is honest; the failure event);
- emission once per session at its end (none during ticks, none on a duplicate end, one per session
  on a switch);
- analysis failure isolation: the session ends cleanly, restore runs, a failure event is recorded and
  no success is claimed; throwing sinks are swallowed.
