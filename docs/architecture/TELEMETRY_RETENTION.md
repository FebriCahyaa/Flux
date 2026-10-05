# Telemetry Retention — rolling 7 days (Step 8.11)

Scope: **only** `/data/adb/.config/zairenkai/telemetry/events/` (Observatory event segments and their
indexes). Retention never touches:
- `installation.json`;
- configuration (`/data/adb/.config/flux`, HiCo);
- `sessions.json`, `session_live.json`, user profiles;
- repository engineering data (source, datasets, evidence, databases, tests), which live in git, not on
  the device.

## Rule

A record is kept while `timestamp_ms ≥ now − 7 × 24 h` (`kRetentionMs`). It is removed once it is older.
A record exactly 7 × 24 h old is kept.

- Whole hourly segments that end at or before the cutoff are deleted. The index is deleted first, so a
  segment without an index is always valid.
- The boundary segment is rewritten atomically (temp + rename) with only the kept lines, and its index
  is rebuilt. Corrupted lines in a boundary segment are dropped.
- Events already older than 7 × 24 h at write time are refused.

## When it runs

| Trigger | Where |
|---|---|
| daemon start | `flux_observatory::start()` after the store opens |
| periodic maintenance, hourly | `maintain_if_due()` in the main loop. Outside sessions the loop now wakes hourly (`idle_timeout_ms()`) instead of sleeping indefinitely; during sessions it already wakes every second |
| session completion | `TeeEventSink`: a persisted `SESSION_END` runs maintenance if it is due |

## Time source and clock safety

- **Retention time source:** the wall clock (`system_clock`, epoch ms), because event timestamps are
  wall-clock epoch ms (`EVENT_MODEL.md`).
- **Clock unset** (before 2020-01-01, for example right after boot without network time): retention is
  skipped. Events with such timestamps are rejected by validation anyway.
- **Clock moved backwards:** the cutoff moves earlier, so less is deleted. That direction is safe.
- **Forward jump:** within one daemon process, the wall clock's advance is compared with the steady
  clock's. If the wall clock moved more than 24 h further than real elapsed time, retention is
  **deferred** for that run. The new time becomes the baseline, so a genuinely corrected clock is
  honoured at the next hourly run.
  - Limitation: a jump that happens while fluxd is not running can't be detected. It is still bounded
    by the "unset" check.
- **Dry run** (`fluxd telemetry retention`) reports what would be removed without deleting.

## Interruption safety

Every step leaves a valid state. An interrupted trim leaves either the old segment or the new one,
never a mix (rename is atomic). An interrupted delete leaves a segment without an index (valid) or an
orphan index, which the next maintenance removes. `*.tmp` leftovers are removed at open and during
maintenance.

## Report

`RetentionReport`: `ran`, `dry_run`, `skipped_reason`, `cutoff_ms`, `segments_deleted`,
`segments_trimmed`, `orphans_removed`, `records_removed`. The daemon logs it.
