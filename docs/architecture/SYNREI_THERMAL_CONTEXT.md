# Synrei Thermal Context (Step 8.9 — read-only)

Status: **IN PROGRESS.** `jni/thermal/ThermalContext.*` (neutral interface and history) and
`jni/thermal/SynreiThermalAdapter.*` (`flux::thermal`, NDK `FluxThermal`, host `flux_thermal`). The
runtime metrics sampler uses it in fluxd (`jni/SessionHost.cpp`). Device: NOT_TESTED.

## Ownership

| Synrei (HiCo, `hicod`) | Zairenkai (fluxd) |
|---|---|
| thermal policy, thermal state, safety ceiling, thermal control | observes Synrei's published state; consumes it as evidence; never overrides it |

Zairenkai does not implement thermal control. It doesn't change thermal limits, unlock protection,
write thermal nodes, or change GameRuntime or SessionRecorder.

## Boundary

```
hicod ──writes──▶ /dev/hico/state            (existing HiCo output, key=value, 0600 root, removed on stop)
                       │ read-only (flux::kernel::ReadOnlyFs, no write API)
                       ▼
SynreiThermalAdapter ──▶ ThermalSnapshot      (neutral; nothing Synrei-specific beyond the adapter)
                       ▼
RuntimeMetricsSampler ── ThermalHistory ──▶ bottleneck::ThermalContext ──▶ BottleneckModel
```

BottleneckModel depends only on `bottleneck::ThermalContext`. It knows nothing about Synrei or the file
format.

## Snapshot fields

| Field | Synrei source | When missing |
|---|---|---|
| thermal state | `state` (idle, boost, relaxed, safety, suspended, disabled) | snapshot unreadable → UNKNOWN |
| thermal headroom | not published by Synrei | **UNKNOWN** (never computed from trips by Zairenkai) |
| CPU, GPU, battery temperature | `cpu_temp`, `gpu_temp`, `battery_temp` (°C, empty = not available) | UNKNOWN with a reason; outside −40…150 °C is "malformed" |
| thermal slope | derived: change in CPU temperature between two verified snapshots from the same Synrei process (°C/min, MEDIUM) | UNKNOWN until two readings; reset when Synrei restarts |
| constraint state | from Synrei's state only (table below) | UNKNOWN |
| timestamp | `timestamp_ms` = steady-clock read time; `source_time_s` = Synrei's `updated` | — |
| source | `synrei:/dev/hico/state` | — |
| confidence | HIGH when verified, otherwise UNKNOWN | — |
| readable | file read and parsed (`state` present) | false |
| verified | the Synrei process is alive (`/proc/<pid>`) **and** `updated` is within 15 s (no more than 2 s in the future) | false → nothing is used as current evidence |

## Constraint mapping (Synrei's own state; never temperatures)

| Synrei state | Constraint |
|---|---|
| `safety` (safety guard restored thermal protection) | **Constrained** |
| `boost` (Synrei reports throttling disabled for the game) | **Unconstrained** |
| `relaxed`, `idle`, `suspended`, `disabled`, anything else | **Unknown** (vendor or stock thermal decides; Synrei does not publish whether it throttles) |

A hot device in `idle` or `relaxed` stays **Unknown**. A device in `boost` at 96 °C stays
**Unconstrained**. A high CPU or GPU temperature never creates a constraint.

## Unavailable, stale and restart

| Case | Result |
|---|---|
| `/dev/hico/state` absent (hicod not running) | UNKNOWN, "Synrei not running" |
| unreadable | UNKNOWN, unreadable |
| malformed (no `state`) | UNKNOWN, malformed |
| file left by a crashed hicod (pid not alive) | readable but **not verified**; constraint and temperatures not exposed |
| `updated` older than 15 s, or in the future | not verified ("stale"); not current evidence |
| hicod restarted (new pid, fresh file) | verified again; slope history reset |
| the adapter or source throws | UNKNOWN snapshot with the error. The sample, the session and the game are unaffected |

`ThermalHistory::at(t)` uses only the **latest** snapshot at or before `t`. It must be usable
(readable, verified, with a known constraint) and no more than max(2 × interval, 5 s) old. A newer
snapshot supersedes older ones, so a state without a constraint claim also ends an earlier one.

## Bottleneck integration

The sampler stores one snapshot per sample and exposes `thermal.constraint`, `thermal.cpu_temp_c`,
`thermal.gpu_temp_c`, `thermal.battery_temp_c`, `thermal.headroom_c` and `thermal.slope_c_per_min`
as metrics in its snapshot. `assess()` passes the session's `ThermalHistory` as the thermal context.
Constrained becomes `ThermalState{throttling=true}`, and Unconstrained becomes
`ThermalState{throttling=false}`. `cap_ratio` stays unknown because Synrei doesn't publish one.

The existing rules and thresholds are unchanged:
- no real thermal evidence means thermal UNKNOWN;
- sustained Synrei `safety` with a measured FPS shortfall can make thermal LIKELY or CONFIRMED and the
  primary finding.

## Safety

The adapter reads only `dev/hico/state` and `proc/<pid>`, through `ReadOnlyFs`, which has no write
operation. Tests prove that a fake tree with thermal nodes is unchanged and that every path read
belongs to that set. On a real temp tree, the state file and a thermal node keep their mtime.

## Tests

`tests/synrei_thermal_context_test.cpp` covers:
- available, with slope;
- unavailable (absent file, crashed hicod);
- missing temperature and malformed values;
- unreadable source and malformed content;
- stale, future and the boundary case;
- constraint mapping, including hot-but-not-constrained;
- confidence and the history (gap, supersede);
- the BottleneckModel receiving thermal evidence: safety gives thermal as primary, boost gives CPU,
  no Synrei gives UNKNOWN;
- Synrei failure isolation, including a restart;
- zero writes, on both the fake and the real filesystem.

## Used by the Decision Engine (Step 8.13)

Only a verified, fresh snapshot counts:
- `safety` (Constrained) forbids BOOST and can lead to MITIGATE or RESTORE;
- `boost` (Unconstrained) permits considering MITIGATE or BOOST;
- every other state, and stale or unverified context, is thermal UNKNOWN, which means OBSERVE and is
  never assumed relaxed.

Temperatures are not used. See `POLICY_DECISION.md`.
