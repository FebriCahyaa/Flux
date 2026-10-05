# Adaptive Optimization V1 (Phase 5)

**Status:** IMPLEMENTED on the host.
**Android CI:** not checked.
**Device:** NOT_TESTED.

Kernel controls are still unverified on devices (B-37), so on real hardware the executor blocks
and the adaptive loop never sees an applied intervention.

## Goal

Keep an executed intervention only when evidence shows that it improves **sustainable**
frame delivery. Otherwise put it back and wait. The goal is maximum sustainable capability, not
maximum clocks.

## Loop

Observe → Understand → Decide → Verify capability → Execute → Verify execution → **Observe impact →
Compare → Keep or Rollback → Cooldown** → Continue observation.

| Step | Component |
|---|---|
| Observe | `RuntimeMetricsSampler` (metrics, FPS from the SessionRecorder slot, Synrei snapshot); unchanged |
| Understand | in-session `assess()`; unchanged |
| Decide | `DecisionEngine`; unchanged |
| Verify capability / Execute / Verify execution | `PolicyExecutor` + Transaction Engine; unchanged except a lifetime fix (below) |
| Observe impact, Compare, Keep/Rollback, Cooldown | **`jni/adaptive/`**: `AdaptiveController`, `InterventionEvaluation` |
| Rollback | `PolicyExecutor::execute(RESTORE)`: the executor finishes its own transaction (read-back verified) |

The adaptive code is pure. It has no I/O, no clock, no thread and no persistence. It is driven by
`LivePolicyController` on the existing session tick, once per fresh sampler sample. It never
writes a node, never builds a `RuntimePlan`, never chooses a value and never calls the
Transaction Engine.

## Baseline

The newest 10 samples (`kBaselineWindow`) are kept while no intervention is active. Each sample
holds:
- FPS and target refresh, kept as separate values (Hz ≠ FPS);
- CPU busiest-core load and GPU load;
- CPU and GPU clock ratios;
- the Synrei state as reported;
- the live bottleneck kind and rating.

Missing values stay `nullopt` and are never 0. A BOOST is admitted only with at least 3 baseline
samples that have both FPS and target (`kMinBaselineFps`). A safety-driven MITIGATE needs no
performance baseline, because safety takes precedence.

## Evaluation

When an intervention is applied, the baseline is frozen and a post window starts. On each fresh
sample the windows are compared. FPS and shortfall are **medians**, so a single spike is not
evidence. Loads and clock ratios are means.

For a **BOOST**, the checks run in this order:
1. Synrei `safety` in any post sample → **ROLLBACK** (`thermal_safety_after_intervention`).
   Sustainability outweighs performance.
2. Fewer than 3 post samples with FPS → **OBSERVE**. At 10 post samples (`kMaxPostSamples`) → **ROLLBACK** (`no_fps_evidence`).
3. Bottleneck relationship: if another resource becomes the likely or confirmed limit (and the
   evidence is not in conflict), the shift is recorded. The intervention is still judged on frame
   delivery, not on whether its target remains the limit.
4. Benefit means one of:
   - the median shortfall drops by ≥ 0.05 of target (`kMinShortfallGain`);
   - the median FPS rises by ≥ 5 % (`kMinFpsGain`).

   A write that succeeded, a higher clock or a utilisation change is never a benefit on its own.
5. No verified Synrei state in the post window → **OBSERVE** (`awaiting_thermal_evidence`).
   At the window limit → **ROLLBACK** (`thermal_evidence_lost`).
6. Benefit → **KEEP** (`benefit` / `benefit_bottleneck_shifted`).
7. No benefit and the target's load rose by ≥ 0.10 (`kCostLoadRise`) → **ROLLBACK** (`cost_without_benefit`).
8. Otherwise **OBSERVE** until the window is full, then **ROLLBACK** (`no_benefit`).

A **MITIGATE** relieves thermal load and is never rolled back for performance reasons. It is
KEPT once 3 post samples exist.

Execution outcomes:
- `ApplyFailed` / `VerifyFailed` (the engine already rolled back, including partial application) → failure cooldown.
- `Blocked`, `NoChange`, `AlreadyActive` → no intervention.
- `RestoreFailed` → no further interventions this session.

## Keep, rollback and observe

These are evaluation outcomes, not new `PolicyDecision` actions; the DecisionEngine vocabulary is
unchanged.
- **KEEP:** the transaction stays applied.
- **ROLLBACK:** `LivePolicyController` asks `PolicyExecutor` to RESTORE its own transaction. The
  decision record says so: action `RESTORE`, constraint `RestoreRequired / adaptive / <reason>`.
- **OBSERVE:** wait for more evidence; nothing escalates.

## No escalation, hysteresis and cooldown

- **No escalation:** while an intervention is evaluating or kept, no other BOOST or MITIGATE is admitted.
  - "FPS still low → raise the clock again" cannot happen.
  - A stronger step needs the intervention restored, a cooldown, then a new DecisionEngine evaluation on fresh evidence.
- **Cooldowns** use evidence timestamps (session tick time, not the wall clock):
  - 30 s after KEEP;
  - 60 s after ROLLBACK or any restore;
  - 60 s after a failed execution.
- **Hysteresis:**
  - A rolled-back or failed intervention is not admitted again under an identical evidence signature.
  - The signature is: action and target, bottleneck kind/rating/conflict, Synrei state, profile, and the FPS shortfall share in tenths.
  - After 2 rollbacks of the same action and target, that intervention is held for the rest of the session.
  - The baseline restarts after every restore.

## Thermal safety

Synrei is read exactly as the executor and DecisionEngine read it: verified + readable → its state;
otherwise unknown. Temperatures are never used to infer safety. Under safety, BOOST is never
admitted by the executor, and a BOOST already applied is rolled back by the adaptive loop or
restored by the DecisionEngine RESTORE rule.

## Ownership

| Component | Owns |
|---|---|
| GameRuntime | profile lifecycle and its own transactions; unchanged |
| Synrei (HiCo) | thermal control; unchanged, read only |
| PolicyExecutor | approved policy transactions; the only execution boundary |
| Transaction Engine | the only write primitive |
| AdaptiveController | judgement of an intervention's outcome only |

## Telemetry

There is no new event type and no schema change (v1, 19 types). B-35 stays deferred: no raw sample
persistence. The trail of an adaptive outcome on device is:
- applies and rollbacks appear as the existing `TRANSACTION_*` events, through the transaction observer;
- the reason is in the RESTORE decision and the `Policy` log line.

## Determinism

The same evidence sequence gives the same verdicts: no randomness, no clock, no learning and no
state outside the session (`reset()` at session start).

## Fixes made while implementing

- **PolicyExecutor (Phase 4B) lifetime bug.** The executor's transaction keeps the write and
  observer callbacks until RESTORE, but those callbacks referred to local variables of `execute()`.
  ASan reported a stack-use-after-return on restore. They now share a heap probe. Behavior is otherwise unchanged.
- Test-only dangling references in `observatory_test` and `observatory_analysis_test` were fixed.
  The whole suite now passes under ASan.

## Known limitations

- **B-42 (new):** under Synrei safety, the DecisionEngine recommends MITIGATE. With that executor
  transaction active it then returns RESTORE on the next sample, because its RESTORE rule
  (safety + active transaction) does not distinguish MITIGATE from BOOST. Phase 5 does not change
  the DecisionEngine. The adaptive cooldown bounds the resulting MITIGATE → RESTORE cycle to one per
  60 s, and the owner must decide the DecisionEngine rule.
- The thresholds are initial values, not calibrated on devices (as for B-33C).
- The first physical adaptive run must be supervised and use one narrowly bounded intervention.
