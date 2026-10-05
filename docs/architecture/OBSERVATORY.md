# Zairenkai Observatory (foundation)

Step 6, branch `integration/game-runtime-clean`. Code: `jni/observatory/` (`FluxObservatory`).
Event schema and registry: `EVENT_MODEL.md`.

## Principle

The Observatory **observes**. Engine components report what they did and why; the Observatory
validates, orders, stores and serves those reports. It never decides policy, never changes a
node, and never explains beyond the recorded evidence.

```
GameRuntime  Performance  Session  Transaction  Kernel*  Graphics*  Thermal*      (* future)
      \            |          |          |          |         |         /
       +--------------------- EventSink::write --------------------------+
                                    |
                     validate  ->  order (timestamp, sequence)  ->  store
                                    |
                           EventSource::query
                                    |
              Timeline / Diagnostics / Explanations / future WebUI
```

## Ownership

| Concern | Owner |
|---|---|
| What happened and why (event content) | the producing component (GameRuntime, SessionManager, ...) |
| Schema, registry, validation, ordering | Observatory |
| Storage medium, retention | Observatory — **not implemented yet** (interfaces only) |
| Decisions (policy) | engines; never the Observatory |
| `sessions.json`, `session_live.json` | SessionRecorder — **unchanged**; the Observatory is additive |

## Producer / consumer contract

- Producers depend only on `EventSink` (`write(Event) -> WriteResult`). A rejected event is
  counted and returned with errors; it never blocks the producer's own work.
- Consumers depend only on `EventSource` (`query(EventQuery)`), filtering by session, category,
  type, source, transaction, minimum severity and time range, ordered oldest first.
- `MemoryEventStore` is the reference implementation (bounded, oldest dropped first) used by tests
  and available for in-daemon diagnostics.

## Integration bridge (Step 6.5)

Producers do not depend on the Observatory. Each exposes an optional neutral observer, called only
**after** a successful state transition, and the bridge (`jni/bridge/ObservatoryBridge.*`) turns
those notices into v1 events:

```
SessionManager ── SessionNotice ──┐
GamePerformanceRuntime ─ RuntimeNotice ─┤
Transaction Engine ── TxNotice ────────┼─> ObservatoryBridge ─> EventSink (fluxd: MemoryEventStore, 1024)
(LaunchBoost uses the same TxObserver) │        stamps session_id from SessionManager context
                                       └─ SessionManager::set_context(id) before begin, "" after end
```

### Event producer table

| Producer | Notice | Events | Evidence carried |
|---|---|---|---|
| `SessionManager` | `SessionNotice` | SESSION_START, SESSION_END, SESSION_SWITCH | package, pid, uid, end reason, duration, clean |
| `GamePerformanceRuntime` | `RuntimeNotice` | RUNTIME_ACTIVATE, RUNTIME_RESTORE, RUNTIME_FAILURE | profile, refresh target, launch boost state, error text |
| `GamePerformanceRuntime` | `RuntimeNotice` | PROFILE_APPLIED, PROFILE_RESTORED | field → source (`preset gaming`, `game override`, …) |
| `GamePerformanceRuntime::recover` | `RuntimeNotice` | RECOVERY_START, RECOVERY_SUCCESS, RECOVERY_FAILED | found / restored / failed / corrupted, journal kept/removed |
| `Transaction` (incl. launch boost) | `TxNotice` | TRANSACTION_BEGIN/APPLY/VERIFY/ROLLBACK/RESTORE | node → original / requested value, failing operation |

### Ownership rules

1. A producer reports only what it did, after it did it (apply → verify → report). It never reports
   intentions, and never waits for or reacts to the Observatory.
2. The bridge owns translation: type, source, severity, result, schema limits (snapshots clipped to
   64 keys / 512 chars; empty reason → `reason unavailable`).
3. The store owns validation and ordering. Rejections are counted, not retried.
4. Session id comes only from `SessionManager` (context hook); events outside a session carry none.
5. `flux.log`, `sessions.json`, `session_live.json` are untouched; the bridge writes no log lines.

### Verified rollback / restore results (Step 6.5.1)

`TRANSACTION_ROLLBACK` and `TRANSACTION_RESTORE` report the verified outcome from the Transaction
Engine's read-back comparison (`after.restored`, `after.not_restored`):

| Engine outcome | `result` | `severity` |
|---|---|---|
| SUCCESS — every node read back at its snapshot | `ok` | rollback: warning (it follows a failure) · restore: info |
| PARTIAL — some nodes at snapshot, some not | `partial` | error |
| FAILED — no node at its snapshot | `failed` | error |

### Failure handling

| Failure | Effect on the producer | Where it is visible |
|---|---|---|
| no sink (Observatory unavailable) | none | `bridge.stats().dropped` |
| sink throws | none (caught in the bridge) | `stats().failed` |
| event rejected by validation | none | `stats().rejected`, `store.rejected()` |
| observer callback itself throws | none (caught in Transaction / GameRuntime / SessionManager) | — |
| store full | oldest event dropped | `store.dropped()` |

Tests (`tests/observatory_bridge_test.cpp`) prove identical node values, journals and states with a
working, missing, throwing and rejecting Observatory, and with throwing observers.

## Implemented now

| IMPLEMENTED | NOT_IMPLEMENTED |
|---|---|
| event schema v1, registry (SESSION, RUNTIME, PERFORMANCE, TRANSACTION, RECOVERY) | ~~producers wired~~ done in Step 6.5 |
| validation (malformed, missing fields, severity/confidence/result, timestamp, type, source) | on-device storage (telemetry files), 7-day retention (Phase 5) |
| JSONL serialisation, corrupted-line handling | index, timeline, explanation generation (Phase 6) |
| write/query/ordering interfaces + bounded memory store | WebUI Observatory (Phase 10) |
| producers wired via bridge; in-memory store in fluxd (Step 6.5) | reading events out of fluxd (CLI/export) |
| host tests | KERNEL / GRAPHICS / THERMAL categories |

## Future integration points (not built)

| Producer | Events it will emit | Hook that exists today |
|---|---|---|
| SessionManager | SESSION_START/END/SWITCH with `session_id`, end reason, package/pid in `after` | `SessionManager::begin/end`, `events()` |
| GamePerformanceRuntime | RUNTIME_*, PROFILE_REAPPLIED, RUNTIME_RESOLVE_FAILED (profile explanation as evidence) | `RuntimeDeps::log` lines, `ResolvedProfile::explain()` |
| PerformancePlanner / LaunchBoost | PERFORMANCE_PLAN (category report), LAUNCH_BOOST_*, REFRESH_REQUEST | `PerformancePlanResult::report`, `LaunchBoost::last_cancel()` |
| Transaction Engine | TRANSACTION_* with `transaction_id`, before/after per node | `Transaction::log()`, journal entries |
| Recovery | RECOVERY_RUN / RECOVERY_INCOMPLETE with restored/failed/corrupted counts | `RecoveryReport` |
| Kernel Intelligence (Phase 3) | KERNEL_* (classification, capability changes) | — |
| Graphics (Phase 7) | GPU/refresh/frame events | — |
| Synrei (Phase 9) | THERMAL_* state transitions, coalesced (`… maintained for Ns`) | HiCo `current_profile`/journal today |

Storage location for device telemetry is already decided at the directive level
(`/data/adb/.config/zairenkai/telemetry/`) but not created in this step; the frozen
`/data/adb/.config/flux/` files are not touched.

## Bottleneck events (Step 8.10)

Two registered types, category `performance`, source `bottleneck`, session id required:
- `BOTTLENECK_ASSESSED`: the final per-session assessment, with its evidence in `after`;
- `BOTTLENECK_ANALYSIS_FAILED`: the analysis could not complete.

The schema version is unchanged (additive types only). In fluxd they go to the bounded in-memory store;
there is still no persistence or retention. See `BOTTLENECK_RESULT.md`.

## Persistent storage and retention (Step 8.11)

The memory store stays. Producers now write through `TeeEventSink`: memory first, then the persistent
store at `/data/adb/.config/zairenkai/telemetry/` (hourly JSONL segments plus indexes), with a strict
rolling 7 × 24 h retention. Persistence failures are isolated and reported once per streak as
`OBSERVATORY_STORAGE_FAILED`, in memory only. See `OBSERVATORY_STORAGE.md` and `TELEMETRY_RETENTION.md`.
This supersedes the earlier "no persistence" notes.

## Historical analysis (Step 8.12)

A read-only analysis layer reconstructs session timelines from the persisted events and correlates
lifecycle, transactions, recovery, the recorded bottleneck result, thermal evidence and FPS evidence
into evidence-backed explanations and bounded seven-day history patterns. It never writes and never
re-rates. CLI: `fluxd telemetry session | analyze | history`. See `OBSERVATORY_ANALYSIS.md`.

## Policy decisions (Step 8.13)

The Decision Engine (`POLICY_DECISION.md`) can consume history patterns from the analysis layer. No
event type was added: decisions are not executed and not yet produced at runtime. A `POLICY_*` event
will be considered only together with a future executor.
