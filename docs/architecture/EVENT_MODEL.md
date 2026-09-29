# Observatory Event Model

Step 6 (Observatory foundation), branch `integration/game-runtime-clean`.
Code: `jni/observatory/Event.{hpp,cpp}` (`flux::observatory`) · Tests: `tests/observatory_test.cpp`.
Status: **Step 6 IN PROGRESS** — model, registry, validation, serialisation implemented and
host-tested; **no producer emits events yet**; linked into `fluxd` as `FluxObservatory`, unused.

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

## Registry (initial)

| Category | Types | Allowed sources | Requires |
|---|---|---|---|
| SESSION | `SESSION_START`, `SESSION_END`, `SESSION_SWITCH` | session | session_id |
| RUNTIME | `RUNTIME_ACTIVATED`, `RUNTIME_RESTORED`, `PROFILE_REAPPLIED` | game_runtime | session_id |
| RUNTIME | `RUNTIME_RESOLVE_FAILED` | game_runtime | — |
| PERFORMANCE | `PERFORMANCE_PLAN` | performance | — |
| PERFORMANCE | `LAUNCH_BOOST_START`, `LAUNCH_BOOST_END`, `REFRESH_REQUEST` | performance | session_id |
| TRANSACTION | `TRANSACTION_APPLIED`, `TRANSACTION_FAILED`, `TRANSACTION_ROLLBACK`, `TRANSACTION_RESTORED` | transaction, game_runtime | transaction_id |
| RECOVERY | `RECOVERY_RUN`, `RECOVERY_INCOMPLETE` | recovery, session, game_runtime | — |

New types are added with `EventRegistry::add` (unique, UPPER_SNAKE_CASE, at least one source).
Kernel, Graphics and Thermal categories are **not** registered yet; they are added when those
engines exist (see `OBSERVATORY.md` integration points).

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
