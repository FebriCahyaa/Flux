# Session Model

Step 5 of `GAME_RUNTIME_MIGRATION_PLAN.md` (branch `integration/game-runtime-clean`).
Code: `jni/session/SessionManager.{hpp,cpp}` (`flux::session`), daemon glue `jni/SessionHost.{hpp,cpp}`.
Tests: `tests/session_manager_test.cpp`.
Status: **Step 5 IN PROGRESS** — implemented, host-tested, wired into `fluxd`; device NOT_TESTED.

## Ownership

```
Main.cpp            game events only: detected (begin), focus lost / process death / failure / daemon stop (end),
    |               profile script ran (profile_applied), loop wake (tick)
    v
SessionManager      one active session; begin/end ordering; cleanup ordering; recovery once per daemon run
    |
    +-- 1. GameRuntime participant   -> GamePerformanceRuntime (owns the performance transaction,
    |                                   apply/restore, journals)
    +-- 2. SessionRecorder participant -> existing recorder (sessions.json / session_live.json unchanged)
```

| Concern | Owner |
|---|---|
| Game detection, focus, PID tracking, DND, thermal tiering | `Main.cpp` (unchanged) |
| When a session begins/ends; order of start, stop, cleanup | **SessionManager** |
| Performance lifecycle, transaction apply/restore, journals | GameRuntime (`GamePerformanceRuntime` via `RuntimeHost`) |
| Statistics and their files | `SessionRecorder` (format unchanged) |
| Render thread placement | `RenderBooster`, still started/stopped by `Main.cpp` next to the session (re-evaluated every pass from preferences) |

## Semantics (ported from the old branch: begin/end, idempotency, single session, EndReason)

| Call | Behaviour |
|---|---|
| `recover()` | Once per daemon run, participants in order. `begin()` calls it first if the daemon did not, so recovery can never be skipped. |
| `begin(key, now)` | Same package + pid while active → no-op (`false`). Another game or a restarted process → `end(Switch)` first. New session id `s-<ms>-<seq>`; participants begin in order (GameRuntime, then SessionRecorder). |
| `end(reason, now)` | Nothing active → no-op (`false`, no participant called). Otherwise participants end in **reverse** order (SessionRecorder finishes its statistics, then GameRuntime restores). A participant that fails to clean up is reported (`false`) but never stops the others; the session ends regardless. |
| `profile_applied()` / `tick()` | Forwarded only while a session is active. `needs_tick()` true while any participant needs a deadline (launch boost). |

`EndReason`: `exit`, `focus_lost`, `process_death`, `switch`, `failure`, `daemon_stop`.
Mapped for GameRuntime: focus_lost → exit (same restore), others 1:1.

Main.cpp mapping: PID tracker death and "exited while applying profile" → `process_death`;
3-strike focus loss → `focus_lost`; PID not resolvable → `failure`; game removed from the list →
`exit`; daemon shutdown → `daemon_stop`.

## Not ported (by decision)

`Analyze`, `Arming`, `ProviderPlan`, provider status, compatibility context, identity logic,
Zygisk, Resolver. The old `SessionRuntime`'s journal handling is not duplicated: journals stay
owned by GameRuntime.

## Not changed

`sessions.json` and `session_live.json` formats and SessionRecorder behaviour. The session id is
not written anywhere yet (Phase 5 decides telemetry storage). `SessionManager::events()` keeps a
bounded in-memory lifecycle log (64 entries) for diagnostics.

Behaviour difference from `main`: `SessionRecorder::start` is now called once per session instead
of on every profile pass (it was idempotent), and it stops before the GameRuntime restore.

## Tests (written first; red → green)

session begin, duplicate begin (same process no-op; restarted process and other game → switch
with reverse-order end), session end, duplicate end (no participant called), process-death end
reason, daemon restart recovery (once; implicit before first begin), cleanup ordering (reverse,
failed participant does not block restore), profile/tick forwarding only while active.

## Status

| IMPLEMENTED | NOT_IMPLEMENTED |
|---|---|
| SessionManager, participants, fluxd wiring | session id / history in telemetry (Phase 5) |
| begin/end ordering, single session, EndReason | RenderBooster as a participant |
| host tests | device validation |

## Runtime metrics participant (Step 8.8)

fluxd's SessionManager now has three participants, in begin order: `game_runtime`, `session_recorder`,
`runtime_metrics`. `runtime_metrics` (`SamplerParticipant`) only starts, ticks and stops the read-only
sampler. Its `end()` always reports clean, its failures never change the session, and it ends
**first**, so sampling stops before the game runtime restores. While a session is active,
`needs_tick()` is true, so the main loop polls with its 1 s timeout during sessions only. Details are
in `RUNTIME_METRICS_SAMPLING.md`.

## SessionRecorder FPS observation (Step 8.8.1)

SessionRecorder (participant `session_recorder`) also publishes each per-second FPS reading into a
read-only `FpsObservationSlot`. That reading carries a steady-clock timestamp, the value, validity and
the source; the slot is cleared on `stop()`. The `runtime_metrics` participant reads it while sampling.
Session order, `sessions.json` and `session_live.json` are unchanged. See `FPS_OBSERVATION.md`.

## Final bottleneck assessment at session end (Step 8.10)

When the `runtime_metrics` participant ends (first, before the game runtime restores), the sampler
computes the session's bottleneck assessment once and hands the result to the Observatory as
`BOTTLENECK_ASSESSED`, or `BOTTLENECK_ANALYSIS_FAILED` if it fails. That participant's `end()` still
always reports clean, analysis failures never change the session, restore or transactions, and no
worker keeps running after the session. See `BOTTLENECK_RESULT.md`.

## Session events on disk (Step 8.11)

`SESSION_*` events (and every other accepted event) are now also persisted to the telemetry store and
kept for 7 × 24 h. Package queries resolve through `session_package` in the segment indexes. A
persisted `SESSION_END` triggers due retention. `sessions.json` and `session_live.json` are unchanged.
Disk failures never affect the session.

## Reconstructed sessions (Step 8.12)

Persisted session events can be rebuilt into a timeline: start, switch (previous session), profile,
runtime, transactions, recovery (by time window), bottleneck and end (end_reason exit / focus_lost /
process_death / switch / failure / daemon_stop, duration, clean). A missing start or end is reported as
incomplete, never filled in. `fluxd telemetry session <id>` prints it. See `OBSERVATORY_ANALYSIS.md`.

## Session state as decision input (Step 8.13)

The Decision Engine reads the session and transaction state (game active, `TxState`, transaction id,
recovery outcome) only to decide whether RESTORE takes priority. It never changes the session or the
transaction. It is not called from fluxd yet. See `POLICY_DECISION.md`.
