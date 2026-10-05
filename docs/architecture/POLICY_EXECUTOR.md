# Controlled Policy Executor (Phase 4B)

Status: **IN PROGRESS** (implementation complete on the host). `jni/policy/PolicyExecutor.*` (`flux::policy::PolicyExecutor`,
part of `flux_policy` / NDK `FluxPolicy`). Called by fluxd since Phase 4C through `LivePolicyController` (`CONTROL_EXECUTION.md`). All kernel
facts stay unverified on devices until validation (B-37), so on real devices the executor blocks.
Device: NOT_TESTED.

## Decision ≠ Execution

```
Observe → Understand → Decide → Validate → Execute → Verify → Restore if required → Observe
                       (DecisionEngine)  (PolicyExecutor ─ Transaction Engine)
```

The PolicyExecutor:
- does **not decide**: it executes an approved `PolicyDecision` exactly as given;
- does **not discover** nodes: operations come only from trusted definitions matched against verified
  capability facts;
- does **not optimise**: one fixed, capability-listed step, with no search, escalation, learning or
  loop;
- does **not bypass** the Transaction Engine: every write is a `NodeWriteOperation` inside a
  `runtime::Transaction`.

## Action handling (exactly five)

| Action | Executor |
|---|---|
| NO_ACTION | no-op (`not_executed`) |
| OBSERVE | not executed (`not_executed`), even when every capability is verified |
| MITIGATE | trusted operations at **one listed step down**, transactional |
| BOOST | trusted operations at **the highest listed value within the known maximum**, transactional; only for an approved BOOST |
| RESTORE | `Transaction::finish()` on the executor-owned transaction (existing restore and read-back). Without one: `nothing_to_restore`. GameRuntime and journal recovery restore their own |

The executor never upgrades OBSERVE to MITIGATE or MITIGATE to BOOST. It executes the decision's
`action` and `target` verbatim.

## Trusted operations (the only things that can be written)

| Operation | Resource | Interface pattern (full match) | MITIGATE target | BOOST target | Range |
|---|---|---|---|---|---|
| `cpufreq_scaling_max_freq` | cpu | `sys/devices/system/cpu/cpufreq/policyN/scaling_max_freq` | largest listed value < current | highest listed value > current | `scaling_available_frequencies` within [`scaling_min_freq`, `cpuinfo_max_freq`] |
| `kgsl_max_gpuclk` | gpu | `sys/class/kgsl/kgsl-3d0/max_gpuclk` | same | same | `gpu_available_frequencies` |
| `devfreq_max_freq` | gpu | `sys/class/devfreq/<name>/max_freq` | same | same | `available_frequencies` within [`min_freq`, highest] |

All three are also capability-verification adapters (`CAPABILITY_VERIFICATION.md`), so their facts
can be verified. Targets are only listed values within the known range, and are never invented:
- no list, no readable current value, or a current value outside the range gives `invalid_target`;
- already at the limit gives `no_change`.

Not executable in Phase 4B (`no_trusted_adapter`):
- memory, storage, display and thermal targets;
- the governor and every other control;
- `read_ahead_kb` and `vm.swappiness`, which are verifiable but **owned by the PerformancePlanner**.
  The executor also refuses any `PerformancePlanner::interface_allowed` path
  (`planner_owned_interface`), so it never contends with GameRuntime's per-game transaction.

**MITIGATE semantics (B-38, resolved):** MITIGATE lowers a ceiling one step, relieving thermal and power
load. The Decision Engine now recommends it only under a verified Synrei `safety`; under Synrei `boost`
a CPU/GPU bottleneck yields BOOST or OBSERVE. The executor was not changed and still does not
reinterpret decisions.

## Validation (all before the Transaction Engine; any failure means `blocked`, exact reasons listed)

1. **Decision integrity:** a `decision_id` is present; the target is a resource with trusted
   operations (`invalid_target` / `no_trusted_adapter`).
2. **Decision constraints:**
   - `restore_required`, `capability_blocked`, `capability_restricted`, `conflict`,
     `thermal_unknown` and `history` always block;
   - for BOOST, `thermal_safety` and a `boost` subject with `insufficient_evidence` or
     `profile_intent` also block.

   The executor does not reinterpret them.
3. **Thermal:** BOOST is blocked when the supplied Synrei snapshot is verified `safety`. Stale or
   unknown context is not reinterpreted either way.
4. **Runtime state** (existing contracts):
   - `no_active_game`;
   - `stale_decision` (the decision's session ≠ the current session);
   - `recovery_incomplete`;
   - `invalid_transaction_state` (GameRuntime `TxState` Failed or Restoring).

   GameRuntime `Active` is normal.
5. **Idempotency:**
   - the same `ACTION:target` while the executor's transaction is active gives `already_active`, with
     no writes;
   - a different one gives `conflicting_active_transaction`; RESTORE first, there is no automatic
     switching.
6. **Per capability:**
   - support Yes, else `capability_unsupported`;
   - readable, else `capability_unreadable`;
   - writable, else `capability_not_writable`;
   - **verified**, else `capability_unverified`;
   - rollback, else `no_rollback`;
   - risk Low or Medium, else `unsafe_risk` (Unknown and High included);
   - not planner-owned;
   - safe interface and trusted pattern, else the fact is not an operation.

   Thermal-like interfaces are never operations. Controls that fail are excluded and listed. If none
   remain, the result is `blocked`.

The executor accepts no path, command, shell fragment or write string from any caller. A
`RuntimePlan` is built only from trusted operations.

## RuntimePlan and Transaction boundary

The `RuntimePlan` uses domain `policy`, subject = `decision_id`, and holds one `NodeWriteOperation` per
permitted control. `runtime::Transaction` owns everything else:
- snapshot;
- write-ahead journal (the caller's `JournalSink`);
- apply and exact read-back verify;
- rollback on any failure;
- `finish()` restore by read-back;
- recovery from the journal (`runtime::recover`).

There is no second transaction framework. The executor holds at most one active transaction.

## Result and failure semantics (`PolicyExecutionResult`)

Fields: `execution_id` (`px-<decision_id>`), `requested_action`, `final_status`, `executed`,
`verified`, `rolled_back`, `restored`, `reason`, `blocked_constraints[]`, `transaction_id`,
`affected_capabilities[]` (id, operation, path, before, target, read-back after), `limitations[]`.

| Case | Status | Flags |
|---|---|---|
| blocked before the engine | `blocked` | executed=false |
| write rejected | `apply_failed` | executed=true (partial, not hidden), rolled_back, restored from the rollback read-back |
| read-back differs | `verify_failed` | executed=true, rolled_back, restored as read back |
| applied | `applied` | executed=true, verified=true; transaction kept active for RESTORE |
| RESTORE ok / not | `restored` / `restore_failed` | restored true / false; the journal keeps unrestored entries |

## Observatory

No new event type. The executor forwards the engine's notices to the caller's `TxObserver`. With the
bridge's `transaction_observer()`, execution appears as the existing `TRANSACTION_BEGIN`, `APPLY`,
`VERIFY`, `ROLLBACK` and `RESTORE` events (source `transaction`, session id, transaction id). These are
persisted by Step 8.11 and explained by Step 8.12. Schema v1 and the registry (19 types) are unchanged,
and the B-35 scope is untouched.

## Not in this phase

The fluxd call path, any adaptive loop (escalation, de-escalation, hysteresis, learning, PID,
cooldowns, "try stronger"), new operations, UI, telemetry expansion, and GameRuntime,
PerformancePlanner or DecisionEngine changes.

## Tests

`tests/policy_executor_test.cpp` (6 functions) covers the 33 required cases:
- NO_ACTION and OBSERVE not executing;
- verified MITIGATE (two policies, write-ahead journal, RESTORE) and BOOST (no_change at the limit);
- gating: unverified, unsupported, unreadable, non-writable, High and Unknown risk, no rollback;
- invalid targets (no list, out of range, unknown resource), no trusted adapter, arbitrary or thermal
  paths never executed;
- thermal safety and the decision constraint blocking BOOST, stale thermal not reinterpreted, a
  thermal-unknown decision blocked;
- profile intent alone (via the DecisionEngine) not executing, MITIGATE not becoming BOOST;
- runtime-state blocks;
- apply and verify failures surfaced with rollback through the existing semantics, restore failure
  visible with the journal kept, nothing to restore;
- duplicate and conflicting execution prevented;
- Observatory `TRANSACTION_*` events through the bridge, schema and registry unchanged;
- decision not mutated, capability metadata unchanged, deterministic result, trusted operations only.
