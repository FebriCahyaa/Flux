# Observatory Persistent Storage (Step 8.11)

Status: **IN PROGRESS.** `jni/observatory/TelemetryStore.*`, `jni/observatory/InstallationEpoch.*`, fluxd wiring in
`jni/ObservatoryHost.*` and `jni/Main.cpp`, CLI hook `fluxd telemetry`. Device: NOT_TESTED.

## Memory + disk

```
producers (SessionManager, GameRuntime, Performance, Transaction, Recovery, BottleneckModel)
        │  EventSink (unchanged interface; producers do not know where events go)
        ▼
TeeEventSink ── 1. MemoryEventStore (bounded 1024, validates, assigns event_id + sequence) → its result is returned
             └─ 2. PersistentEventStore.append (best effort, isolated)
```

Only events the memory store **accepted** (valid according to the registry) are persisted, so there is
no second validation path and no false success. Persistent failures never reach producers. GameRuntime,
sessions, transactions and restore keep running.

## Layout (format 1)

```
/data/adb/.config/zairenkai/
  installation.json                       installation epoch — outside telemetry/, never retained
  telemetry/
    FORMAT                                "zairenkai-telemetry 1"
    events/YYYYMMDDTHH.jsonl              events whose timestamp falls in that UTC hour, one JSON object per line
    events/YYYYMMDDTHH.idx                derived index of that segment
```

Nothing else is written there. This directory is runtime telemetry on the device; it is not repository
data, and the retention rules apply only to it.

- **FORMAT:** an unknown version makes the store refuse to write (read-only), so a future format is
  never corrupted.
- **Record:** exactly `to_json(event)` from the Observatory event contract (`EVENT_MODEL.md`), so only
  defined fields are persisted. The event contract (key, value and reason limits) has no room for
  secrets, tokens, command output or files. Records above 48 KiB are refused.
- **Append:** one `write(2)` with `O_APPEND`, then `fdatasync`.
  - If the segment's last byte is not `\n` (a torn line from a crash), a newline is written first, so
    the torn line becomes one isolated corrupted line.
  - Files are mode 0600 and directories 0700.
- **Index (`.idx`, `key=value`):** `bytes`, `count`, `min_ts`, `max_ts`, `max_severity`, `sessions`,
  `sources`, `types`, `transactions`, `packages`, `session_package=<sid>:<pkg>`.
  - It is rewritten atomically (temp + fsync + rename + directory fsync) after each append.
  - It is valid only while `bytes` equals the segment size. Otherwise it is ignored and the segment is
    scanned, so a stale or missing index never hides data.
- **Sessions, bottleneck assessments and recovery** are views over the same event stream: `SESSION_*`,
  `BOTTLENECK_*` and `RECOVERY_*` types, found through the index `types` and `sessions`. There is no
  separate copy.

## Query (`PersistentEventStore::query`)

Filters: date range, `session_id`, `package`, `source`, `type`, minimum `severity`, `transaction_id`, `limit`.

1. Segments outside the date range are skipped by name.
2. For `package`, sessions are found from `session_package` in the indexes alone.
3. Segments whose index cannot match are skipped without being read.
4. Matching segments are read one at a time, line by line. Only matching events are kept.

Results are ordered by `(timestamp, sequence)`. Duplicate `event_id`s are returned once. History is
never loaded into RAM as a whole.

## Corruption and restart

| Case | Behaviour |
|---|---|
| malformed line, partial line, truncated file | that line is counted as corrupted and skipped; every other record is returned |
| crash between data and index | index stale → ignored and scanned; rebuilt on the next append |
| leftover `*.tmp` (interrupted atomic write) | removed at `open()` and by maintenance |
| index without segment | removed by maintenance |
| duplicate event (same `event_id`) | refused at append (recent ids, including those reloaded from the newest segments at `open()`); deduplicated at query |
| restart during write | the next append isolates the torn line; earlier records are intact |

## Failure observability

`StorageHealth` tracks:
- `open`, `format_ok`, `read_only`;
- counts of `written`, `duplicates`, `rejected` and `failed`;
- `last_error` and `failing`.

The first failure of a streak writes `OBSERVATORY_STORAGE_FAILED` (category `observatory`) to the
**memory store only**, together with a log line. If `open()` fails at daemon start, events stay in
memory only.

## Installation epoch

`installation.json` holds:
- `installation_id` (128-bit random, hex);
- `installed_at_ms` (0 when the clock was unset);
- `first_version`, `first_android_version`, `first_kernel_version`;
- `first_device_identity` (manufacturer, model and board platform only; no serial, IMEI or account
  data);
- `architecture`.

It is written once, when absent, and never rewritten. A normal module update doesn't touch
`/data/adb/.config`, so the epoch survives. A corrupt file is preserved and reported, never overwritten.

## CLI (device validation)

`fluxd telemetry status | retention | query [session=…] [package=…] [source=…] [type=…] [tx=…] [severity=…] [from=…] [to=…] [limit=…]`

All subcommands open the store read-only. `retention` is a dry run, because fluxd owns the real cleanup.

## Tests

`tests/telemetry_store_test.cpp` covers:
- persistent write (the line is exactly the contract);
- restart recovery and duplicates;
- corrupted, partial and truncated records, and restart during write;
- the retention boundary, expired deletion, fresh preservation, refusing expired writes, and dry run;
- clock safety;
- interrupted cleanup;
- disk write failure with tee isolation and no false success;
- unusable directory, invalid and oversized events;
- index and query by session, package, type, source, severity, transaction, date and limit;
- installation epoch create, preserve, corrupt and unset clock;
- read-only and unknown-format behaviour.
