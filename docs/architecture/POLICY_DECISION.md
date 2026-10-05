# Decision & Policy Foundation (Step 8.13 — reasoning only)

Status: **IN PROGRESS** (implementation complete; CI not checked; device not tested). Code: `jni/policy/`
- `PolicyEvidence.*`: inputs and evidence references
- `PolicyConstraint.*`: constraints and capability gating
- `PolicyDecision.*`: the decision and its explanation
- `DecisionEngine.*`

NDK `FluxPolicy` is linked into fluxd with **no call path**. Host library: `flux_policy`.

## Decision ≠ Execution

```
Observe → Understand → Decide → Execute → Verify → Observe
                         ▲
                 Step 8.13 ends here
```

`DecisionEngine::evaluate(PolicyInputs) → PolicyDecision` is a pure function. There is **no Policy
Executor**: nothing turns a decision into a `RuntimePlan` or a transaction. The policy code does not
write sysfs, procfs, kernel, thermal, refresh, GPU, CPU, memory or storage nodes, and has no file I/O
at all. It does not change GameRuntime, PerformancePlanner, Synrei, SessionRecorder, the bottleneck
thresholds, Observatory persistence, `sessions.json`, `session_live.json` or event schema v1. Future
flow: `PolicyDecision → PolicyExecutor → RuntimePlan → Transaction → verify → Observatory` (not
implemented).

## Inputs (all read-only evidence from existing layers)

| Input | Source |
|---|---|
| `capabilities` | CapabilityContext (kernel / graphics / display facts: supported, readable, writable, verified, risk, rollback, confidence) |
| `bottleneck` | `flux::bottleneck::BottleneckResult`, consumed as produced (never re-rated) |
| `thermal` | `flux::thermal::ThermalSnapshot` from the Synrei adapter: state, constraint, verified, confidence |
| `fps` | target refresh (Hz), refresh capability (Hz), observed FPS, shortfall samples. From the FPS observation bridge or the assessment evidence; no new collector |
| `profile` | `ProfileMode`, which mirrors `FluxProfileMode`. It is intent, not authority |
| `runtime` | game active, `TxState` of the transaction engine, transaction id, recovery not clean |
| `history` | `observatory::Pattern`s from the Step 8.12 analyzer |
| `evaluation_time_ms` | supplied by the caller; the engine reads no clock |

Missing evidence is absent and stays UNKNOWN. It is never turned into false, zero or "safe".

## Actions (exactly five)

`NO_ACTION`, `OBSERVE`, `MITIGATE`, `BOOST`, `RESTORE`. OBSERVE is the evidence-gathering fallback
whenever a stronger action is not justified.

## Safety hierarchy (evaluation order)

1. **RESTORE**, when the existing runtime or transaction state requires it:
   - `TxState::Failed` (rollback incomplete);
   - `TxState::Restoring` (restore incomplete);
   - `TxState::Active` without an active game;
   - journal recovery not clean;
   - Synrei `safety` while a transaction is `Active`.

   Confidence HIGH. No other restore condition is invented.
2. **NO_ACTION**: no active game and nothing to restore.
3. **Thermal safety** (Synrei `safety`, verified and fresh): BOOST is forbidden. The result is MITIGATE
   if a verified control for the bottleneck resource (cpu or gpu, default cpu) is actionable,
   otherwise OBSERVE. Thermal safety overrides performance intent.
4. **No, conflicting or insufficient bottleneck evidence** (no result, `conflict`, primary unknown or
   rated below LIKELY): OBSERVE at LOW confidence. Conflicting findings are kept as blocking evidence.
5. **History**: `repeated_restore_failure` withholds MITIGATE and BOOST (OBSERVE).
6. **Thermal unknown** (no context, stale or unverified, or a state that does not report a constraint,
   such as relaxed, idle, suspended or disabled): OBSERVE at reduced confidence. It is never assumed
   relaxed.
7. **Synrei `boost`** and a primary resource (cpu, gpu, memory, storage) rated LIKELY or above: the
   capability gate decides.
   - **BOOST** only for cpu or gpu, and only if: rated CONFIRMED, an FPS shortfall was observed, the
     profile requests performance, and the gate is actionable.
   - Otherwise **MITIGATE** if the gate is actionable.
   - Otherwise OBSERVE.

Therefore: RESTORE → NO_ACTION → MITIGATE → BOOST. A profile never forces BOOST, and BOOST needs every
condition.

## Capability gating (`gate(capabilities, resource)`)

Only rollback-capable control facts count, because a later executor must be able to undo them:
- cpu: `cpufreq`, `governor`;
- gpu: `gpu`;
- memory: `swap`, `zram`;
- storage: `io_scheduler`;
- display and thermal: none.

Each id is resolved through the context, so the highest-confidence source wins and conflicts stay
visible.

| Fact | Verdict |
|---|---|
| support unknown, conflicting or No (unsupported) | blocked |
| unreadable | blocked (never treated as writable) |
| not writable | blocked |
| writable but **unverified** | **restricted** (not actionable) |
| verified but risk High | restricted |
| supported, readable, writable, verified, rollback, risk Low or Medium | **actionable** |

A verified fact takes precedence over an unverified duplicate. Today every kernel fact is
`verified=false` (Step 7: no write and read-back proof yet), so on real devices MITIGATE and BOOST
currently resolve to **OBSERVE (restricted)**. That is by design, until a verification step exists.

## PolicyDecision

| Field | Meaning |
|---|---|
| `decision_id` | `pd-<evaluation_time_ms>-<fnv64 of the explanation>`; identical inputs give an identical id |
| `action`, `target`, `confidence` | weakest of bottleneck and thermal confidence; RESTORE and NO_ACTION are HIGH |
| `reason` | correlational wording ("supported by", "consistent with") |
| `supporting_evidence[]`, `blocking_evidence[]` | `{source, ref, value}`, copied verbatim (bottleneck evidence metric and value, capability id and interface, thermal source and state, FPS fields, runtime state) |
| `constraints[]` | `restore_required`, `thermal_safety`, `thermal_unknown`, `capability_blocked`, `capability_restricted`, `conflict`, `insufficient_evidence`, `profile_intent`, `history` |
| `limitations[]` | always includes the FPS line (Hz and FPS kept apart) and "decision only: … nothing on the device was changed" |
| `session_id`, `evaluation_time_ms` | from the inputs |

`explain(decision)` renders what was selected, why, the evidence and the constraints, marked
"recommendation only, not executed". `evaluate()` never throws: a failure yields OBSERVE with the error
as a limitation.

## Observatory

**No new event type.** Decisions are not executed and have no call path in fluxd, so nothing needs
persistent visibility yet. Existing events (transactions, bottleneck, sessions) remain the record.
A `POLICY_*` event would be justified only together with the future executor, as a documented,
registered, tested addition. B-35 (raw sample persistence) is untouched, and a test checks that the
registry is unchanged (19 types) and the schema is v1.

## Tests

`tests/policy_decision_test.cpp` (11 functions) covers the 20 required cases:
- no and unknown evidence;
- stale thermal evidence;
- safety blocking BOOST, and safety with an active transaction giving RESTORE;
- Synrei boost permitting BOOST, and no shortfall giving no BOOST;
- confirmed CPU, and confirmed GPU without a control;
- conflict;
- capability gating: unsupported, unreadable, non-writable, unknown support, unverified, high risk,
  verified, verified-over-unverified;
- restore priority;
- profile intent not forcing BOOST, and LIKELY not being enough;
- determinism and identical decision ids;
- exact evidence references;
- no writes, unchanged inputs and failure isolation, with history blocking;
- B-35 unchanged and the five actions.

## Capability verification (Step 8.14)

The Decision Engine can now receive `verified=true` facts produced by the capability verifier
(`CAPABILITY_VERIFICATION.md`). Nothing in the engine changed. With verified cpufreq, KGSL or devfreq
ceilings, the cpu or gpu gate can become actionable, so MITIGATE or BOOST become possible
recommendations. They are still recommendations only: no executor exists. On real devices this is
unvalidated (B-37).

## Executed by the Policy Executor (Phase 4B)

`PolicyExecutor` (`POLICY_EXECUTOR.md`) executes a `PolicyDecision` exactly as decided, only through
trusted operations and the Transaction Engine. It never upgrades an action and never acts on
OBSERVE or NO_ACTION. There is still no fluxd call path. Open design question (B-38): for a
non-thermal bottleneck under Synrei `boost`, MITIGATE is executed as a one-step ceiling reduction.
