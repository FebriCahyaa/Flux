# Observatory Historical Analysis & Explanations (Step 8.12 — read-only)

Status: **IN PROGRESS** (implementation complete; device validation pending). Code:
- `jni/observatory/SessionTimeline.*`
- `jni/observatory/Explanation.*`
- `jni/observatory/ObservatoryAnalyzer.*`
- `jni/observatory/TelemetryCli.*` (the read-only `fluxd telemetry` commands, now host-testable)
- an additive read-only `PersistentEventStore::newest_timestamp()`

Step 8.12 **consumes** Step 8.11. The telemetry format, layout, retention, epoch, duplicate and
crash-recovery semantics are unchanged.

## Pipeline

```
persisted segments (7 x 24 h)  ──  PersistentEventStore::query (index-assisted, one hourly segment at a time)
        ▼
SessionTimeline      session events (session_id) + context events without a session id (recovery) in its window
        ▼
ObservatoryAnalyzer  correlation: lifecycle · transactions · recovery · bottleneck · thermal · FPS
        ▼
Explanation / SessionSummary / HistoryReport   (to_text for the CLI)
```

## Inputs: what is recorded and what is not

| Fact | Recorded as | Used |
|---|---|---|
| session lifecycle | `SESSION_START` (package, pid), `SESSION_SWITCH` (before.session_id), `SESSION_END` (end_reason, duration_ms, clean) | yes |
| runtime / profile | `RUNTIME_ACTIVATE` / `RESTORE` / `FAILURE`, `PROFILE_APPLIED` / `RESTORED` | yes |
| transactions | `TRANSACTION_BEGIN` / `APPLY` / `VERIFY` / `ROLLBACK` / `RESTORE` (result, restored / not_restored, subject, transaction_id) | yes |
| recovery | `RECOVERY_START` / `SUCCESS` / `FAILED`, no session_id | yes, by time window |
| bottleneck result | `BOTTLENECK_ASSESSED` (primary, rating, confidence, conflict, samples, secondary, note, evidence.*), `BOTTLENECK_ANALYSIS_FAILED` | yes, **as recorded** |
| thermal | only as `evidence.thermal.*` inside `BOTTLENECK_ASSESSED` | yes |
| FPS / target refresh | only as `frame_deficit`, `fps_vs_refresh`, `display.refresh.current_hz` / `max_hz` evidence inside `BOTTLENECK_ASSESSED` | yes |
| Synrei state transitions (safety, boost, relaxed, idle, suspended, disabled) | **not recorded** as events | reported as a limitation |
| per-sample FPS / CPU / GPU metrics | **not recorded** | reported as a limitation |

Note: the real transaction type names are `TRANSACTION_APPLY`, `TRANSACTION_VERIFY` and
`TRANSACTION_RESTORE`; there are no `…_APPLIED` / `…_VERIFIED` / `…_RESTORED` types. Schema v1 is
unchanged.

## Ordering

The EventStore contract applies: timestamp ascending, then `sequence` (write order) as the
deterministic tie-breaker. The timeline keeps every event exactly as stored.

## Session reconstruction

- **Start, end, end reason, duration, clean end, package:** from the stored session events. Duration
  is `after.duration_ms`; when that is absent it is derived only if both start and end are recorded.
- **Transactions:** grouped by `transaction_id` in order of first appearance.
- **Game switch:** the old session shows `end_reason=switch`. The new session shows
  `previous_session_id` from `SESSION_SWITCH`.
- **Recovery events:** attached as *context* only when they fall inside the session window
  (`in_session=false`), with the limitation "no session_id".
- **Missing parts** (no `SESSION_START`, no `SESSION_END`, no assessment) appear in `limitations` and
  `completeness`. Nothing is fabricated: an unknown duration stays unknown and is never 0.

## Evidence model (`Explanation`)

Fields: `topic`, `summary`, `primary_finding`, `supporting[]`, `contradicting[]`, `confidence`,
`time_range`, `related_session_id`, `related_transaction_id`, `limitations[]`.

Each `EvidenceRef` names the stored `event_id`, `event_type`, `timestamp_ms`, `field` (for example
`after.evidence.cpu.0`) and the stored `value`, verbatim. Derived values are marked `derived`.

| Topic | Rules |
|---|---|
| lifecycle | complete → HIGH; incomplete → LOW; an unclean end is contradicting evidence |
| transaction | per id: applied / apply failed / not recorded; verified by read-back / verification failed / not recorded; rollback and restore with restored or not-restored counts. Success is claimed only with matching `ok` events. Apply ok + verify failed, or a non-ok restore, is contradicting evidence (LOW). A full recorded chain is HIGH |
| recovery | outcome from `RECOVERY_SUCCESS` / `FAILED`; associated by time window (limitation) |
| bottleneck | primary, rating, confidence, samples, secondary, evidence copied **as recorded** (never re-rated). Conflict → primary "unknown (conflicting evidence)", the competing categories' evidence becomes contradicting |
| thermal | present only with `evidence.thermal.*`: "thermal constraint reported by synrei (rating)", **correlated with** the other recorded findings. Without it: UNKNOWN, with the limitation that missing, stale, unverified or not-constraining context cannot be told apart. The transition limitation is always present. Never inferred from temperature |
| fps | target refresh (Hz), refresh capability (Hz), observed peak (FPS) and shortfall (samples below target) are separate fields; "Display refresh is not frame rate" |

## Causality

The wording is correlational: "rates", "is correlated with", "reported by", "consistent with". The
analyzer never says "caused", "proven" or "guaranteed" (a test checks this). Bottleneck ratings are
the BottleneckModel's own, and history patterns are repetitions, not causes.

## Unknown handling

Every summary field defaults to `unknown` or "not recorded". Statistics come only from recorded
evidence, and the source event id is given. Unknown is never turned into 0.

## History (`history(package, from, to)`)

- The range is clamped to 7 × 24 h ending at `to` (with a limitation).
- The CLI default `to` is the **newest stored event**, not the current time, so output is
  deterministic.
- Sessions are found through `SESSION_START` with `package` (index-assisted, at most 1000).
- Each session is then processed one at a time with targeted queries (its `BOTTLENECK_ASSESSED`, its
  `TRANSACTION_RESTORE` / `ROLLBACK`). Only counters are kept, so memory stays bounded.
- Patterns are reported only for **two or more** occurrences:
  - `repeated_cpu_bottleneck` / `repeated_gpu_bottleneck`: primary rated at least likely;
  - `repeated_thermal_constraint`: thermal primary or secondary rated at least likely;
  - `repeated_restore_failure`: any non-ok restore or rollback.
- Each pattern carries the occurrence count, sessions considered, time range and up to 20 session ids.
- Pattern confidence is at most MEDIUM. Sessions without an assessment are excluded from bottleneck
  patterns (incomparable), and that exclusion is stated as a limitation.
- No optimisation, tuning or profile change is derived.

## Corruption

Corrupt lines follow `PersistentEventStore`: they are skipped, valid records are used, and the count
appears in `limitations`. Files are never rewritten or repaired by analysis.

## Read-only guarantee and determinism

- The analyzer holds a `const PersistentEventStore&` and only calls `query()` / `newest_timestamp()`.
  It never calls `append()` or `maintain()`.
- It touches no kernel, thermal, performance, GameRuntime, PerformancePlanner, Synrei, SessionRecorder,
  `sessions.json` or `session_live.json` state.
- No clock or randomness is used. The same stored events give byte-identical output, which the tests
  check through a fresh read-only store.
- `fluxd telemetry …` opens the store read-only. `retention` is a dry run, and
  `session` / `analyze` / `history` never write. Tests check that the file sizes and mtimes are
  unchanged.

## CLI

`fluxd telemetry session <session_id>` · `fluxd telemetry analyze <session_id>` ·
`fluxd telemetry history <package> [from=ms] [to=ms]`, plus the Step 8.11 `status`, `retention` and
`query`.

## Tests

`tests/observatory_analysis_test.cpp` covers:
- an empty session;
- normal reconstruction and ordering;
- process death, focus loss and a game switch;
- transaction success, rollback and partial restore;
- the recovery path;
- the bottleneck explanation, with exact evidence and a preserved result;
- thermal correlation, and stale or missing thermal evidence;
- FPS shortfall, with Hz and FPS kept distinct;
- contradictory evidence (conflict, failed verification) and missing evidence;
- corrupt telemetry input;
- seven-day bounded history and patterns;
- deterministic output;
- no mutation, and the read-only CLI.
