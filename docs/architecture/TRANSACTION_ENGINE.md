# Runtime Transaction Engine

Step 1 of `GAME_RUNTIME_MIGRATION_PLAN.md` (branch `integration/game-runtime-clean`).
Code: `jni/runtime/Transaction.{hpp,cpp}` · Tests: `tests/transaction_test.cpp`.
Status: **host-tested, not wired into `fluxd`** (not in `jni/Android.mk`), not device-tested.

## CURRENT

`main` has no transaction layer: `flux_profiler.sh` writes nodes with `apply()` (write + `chmod
444`, no read-back) and only some features save/restore values. The old Game Runtime branch
(`ccr-23925ebb-375wkg`, `jni/compat/Runtime.*`) has a working `Action`/`NodeWrite`/`Transaction`/
`Watchdog` implementation.

## PROBLEM

In the old branch the transaction header is not neutral:
- `Runtime.hpp` includes `Resolver.hpp` and defines `Backend`, `NativeBackend`, `BackendAction`,
  `ProviderReport` — the adapters that carried identity layers into the same transaction.
- State enum `ContextState` comes from the compatibility vocabulary.
- No transaction ID; the journal is written by the session only after all actions ran, so a crash
  between `apply` and that write leaves an unjournaled change.
- Recovery accepted malformed or unsafe lines only by returning false, without reporting them.

## TARGET

A neutral engine that executes approved operations and knows nothing about games, identities,
resolvers or providers.

| Old (`flux::compat`) | New (`flux::runtime`) |
|---|---|
| `Action` | `TransactionOperation` |
| `NodeWrite` | `NodeWriteOperation` |
| list of actions added to `Transaction` | `RuntimePlan { domain, subject, operations }` |
| `ContextState` | `TxState` |
| `Watchdog::encode/decode/recover_verified` | `journal::encode_entry/decode_entry/serialize/parse`, `recover()` |
| — | `make_transaction_id()`, write-ahead `JournalSink` |
| `Backend`, `NativeBackend`, `BackendAction`, `ProviderReport`, `Resolver.hpp` | **not ported** |

A "PerformancePlan" is a `RuntimePlan` with `domain = "performance"`; no separate type is needed.

## OWNERSHIP

| Concern | Owner |
|---|---|
| Which operations are valid, eligible, allowed by mitigation/capabilities | Planner (PerfPlanner, later tweak contracts) |
| Building the `RuntimePlan` | Planner |
| Transaction ID, snapshot, journal, apply, verify, rollback, restore, recovery | **Runtime Transaction Engine** |
| Where the journal is stored, when recovery runs | Session (step 5) |
| Observing results / events | Observatory (Phase 4) |

The engine never adds, drops or rewrites operations except to skip one whose snapshot fails
(node missing/unreadable), which is recorded in `skipped()`.

## FLOW

```
start():  for each operation
            snapshot()            -> fail: skip (recorded), continue
            journal (write-ahead) -> sink fails: rollback, Failed, nothing unjournaled applied
            apply(); verify()     -> fail: rollback, Failed
          all ok -> Active (or Inactive if nothing applied)
reapply(): re-write + verify applied operations (originals kept)
finish():  restore() + verify_restore() in reverse order -> Restored, journal emptied
           any restore failure -> Failed, failed entries stay in the journal
recover(io, text): newest entry first, write original, read back to verify
```

Journal format (v1):
```
flux-journal 1<TAB><tx-id><TAB><domain><TAB><subject>
<path><TAB><original>          (\\, \n, \t escaped)
```
Legacy entry-only journals from the old branch (no header) are accepted as version 0.

## FAILURE HANDLING

| Failure | Behaviour |
|---|---|
| Node missing / unreadable at snapshot | Operation skipped, never written |
| Journal cannot be written | Operation not applied; transaction rolled back and Failed |
| Write fails or value does not read back | Rollback of every touched operation, Failed |
| Restore fails at finish | Failed; those entries remain journaled for recovery |
| Daemon crash mid-session | `recover()` replays the journal, verifying each restore |
| Malformed journal line, relative path, `..`, empty path | Reported in `corrupted`, never written |
| Unknown journal version | All lines reported corrupted, nothing written |
| Any failed or corrupted entry during recovery | `clean() == false` → caller keeps the journal |

Tests cover: snapshot creation, apply, verify success, verify failure, apply failure, journal
failure, rollback, restore (incl. reapply after overwrite), restore failure, crash recovery
(incl. legacy journal and non-sticking restore), corrupted journal handling.
