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

## Implemented now

| IMPLEMENTED | NOT_IMPLEMENTED |
|---|---|
| event schema v1, registry (SESSION, RUNTIME, PERFORMANCE, TRANSACTION, RECOVERY) | producers wired (SessionManager, GameRuntime, Transaction, recovery) |
| validation (malformed, missing fields, severity/confidence/result, timestamp, type, source) | on-device storage (telemetry files), 7-day retention (Phase 5) |
| JSONL serialisation, corrupted-line handling | index, timeline, explanation generation (Phase 6) |
| write/query/ordering interfaces + bounded memory store | WebUI Observatory (Phase 10) |
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
