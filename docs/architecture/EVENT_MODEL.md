# Observatory Event Model

Step 6 (Observatory foundation), branch `integration/game-runtime-clean`.
Code: `jni/observatory/Event.{hpp,cpp}` (`flux::observatory`) · Tests: `tests/observatory_test.cpp`.
Status: Step 6 foundation approved; **Step 6.5 IN PROGRESS** — producers wired through the
integration bridge into an in-memory store in `fluxd` (no persistence).

## Event schema (v1)

| Field | Type | Required | Meaning |
|---|---|---|---|
| `schema` | int | yes | `1` |
| `event_id` | string ≤ 64 | assigned by the store when empty | unique id, `ev-<ts>-<seq>` |
| `sequence` | uint64 | store-assigned | total write order; tie-breaker for equal timestamps |
| `timestamp_ms` | int64 | yes | wall clock, Unix ms; must be ≥ 2020-01-01 and ≤ now + 24 h |
| `source` | lower_snake_case ≤ 32 | yes | producing component; must be allowed for the type |
| `type` | UPPER_SNAKE_CASE | yes | registered event type |
| `severity` | `debug` `info` `notice` `warning` `error` `critical` | yes | |
| `session_id` | string ≤ 64 | per type | `s-<ms>-<seq>` from SessionManager; empty outside sessions |
| `reason` | string ≤ 512 | yes | why; `reason unavailable` when unknown — never invented |
| `before`, `after` | object of string → string (≤ 64 keys) | yes (may be `{}`) | evidence: state snapshots |
| `confidence` | `high` `medium` `low` `unknown` | yes | producer's certainty |
| `result` | `ok` `failed` `partial` `skipped` `unknown` | yes | outcome |
| `transaction_id` | string ≤ 64 | per type | `tx-<ms>-<seq>` from the Transaction Engine |

Serialised as one JSON object per line (JSONL). Category is not stored per event; it is derived
from the type through the registry.

## Registry (Step 6.5)

| Category | Type | Source | Requires | Emitted when (always after the transition) |
|---|---|---|---|---|
| SESSION | `SESSION_START` | session | session_id | all participants began |
| SESSION | `SESSION_END` | session | session_id | all participants ended; `after.end_reason`, `duration_ms`, `clean` |
| SESSION | `SESSION_SWITCH` | session | session_id | new session started after replacing `before.session_id` |
| RUNTIME | `RUNTIME_ACTIVATE` | game_runtime | — | per-game context active (`after.profile`, `refresh_target_hz`, `launch_boost`) |
| RUNTIME | `RUNTIME_RESTORE` | game_runtime | — | context ended and restore finished |
| RUNTIME | `RUNTIME_FAILURE` | game_runtime | — | profiles unreadable / resolve failed / plan rejected / transaction rolled back |
| PERFORMANCE | `PROFILE_APPLIED` | performance | — | per-game transaction applied **and read back**; `after` = field → source |
| PERFORMANCE | `PROFILE_RESTORED` | performance | — | per-game transaction restored |
| TRANSACTION | `TRANSACTION_BEGIN` | transaction | transaction_id | state moved to preparing |
| TRANSACTION | `TRANSACTION_APPLY` | transaction | transaction_id | all writes done (ok) or a write / journal write failed (failed) |
| TRANSACTION | `TRANSACTION_VERIFY` | transaction | transaction_id | all read-backs matched (ok) or one did not (failed) |
| TRANSACTION | `TRANSACTION_ROLLBACK` | transaction | transaction_id | undo after a failure finished (ok / partial) |
| TRANSACTION | `TRANSACTION_RESTORE` | transaction | transaction_id | end-of-lifecycle restore finished; `before` = undone values, `after` = originals |
| RECOVERY | `RECOVERY_START` | recovery | — | journal replay begins at daemon start |
| RECOVERY | `RECOVERY_SUCCESS` | recovery | — | a journal restored cleanly (or none existed) |
| RECOVERY | `RECOVERY_FAILED` | recovery | — | a journal kept: failed or corrupted entries |

The Step 6 placeholder names (`RUNTIME_ACTIVATED`, `TRANSACTION_FAILED`, `RECOVERY_RUN`, …) were
replaced before any producer or storage used them. Kernel, Graphics and Thermal types are not
registered.

## Validation (rejects, never repairs)

| Rejected | Where |
|---|---|
| malformed JSON, non-object, truncated line, unknown key, missing key, wrong type, schema ≠ 1 | `from_json` |
| invalid severity / confidence / result text | `from_json` |
| missing/invalid source, reason, unknown type, source not allowed for type | `validate` |
| missing session_id / transaction_id where the type requires it | `validate` |
| timestamp ≤ 0, before 2020, or more than 24 h in the future | `validate` |
| oversized fields or snapshots | `validate` |

`parse_jsonl` keeps valid lines and reports each corrupted one as `line N: <error>`.

## Event lifecycle

```
producer builds Event (no id, no sequence)
   -> EventSink::write: validate -> reject (counted, not stored) | assign sequence + event_id -> store
   -> EventSource::query: filtered, ordered (timestamp, sequence), optional limit (oldest first)
```

## Step 8.11 additions

- New category `observatory` and type `OBSERVATORY_STORAGE_FAILED` (source `observatory`, no session
  required). It is written to the memory store only and is never persisted.
- The persisted record format is exactly `to_json(event)` (schema 1), one event per line. Events
  rejected by validation are never persisted.
- `event_id` stays the key for duplicate detection on disk.

## Consumers: historical analysis (Step 8.12)

The analysis layer reads events through the existing ordering contract (timestamp, then sequence) and
the stored field names. It relies on `SESSION_END.after.{end_reason,duration_ms,clean}`,
`SESSION_SWITCH.before.session_id`, `TRANSACTION_*` result and `after.{restored,not_restored,subject}`,
and `BOTTLENECK_ASSESSED.after.*`. Schema v1 is unchanged, and no event type was added or renamed.
Synrei state transitions and per-sample FPS are not events; the analysis reports them as limitations.
