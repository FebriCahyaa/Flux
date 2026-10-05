# Control Execution — Live Decision Integration (Phase 4C)

Status: **IMPLEMENTED on the host**. Android CI: not checked. Device: NOT_TESTED. On devices every
kernel control is still unverified (B-37), so live execution blocks until device validation.

## Loop

Observe → Understand → Decide → Validate → Execute → Verify → Restore if required → Observe.

| Step       | Component (existing)                                     |
|------------|----------------------------------------------------------|
| Observe    | `RuntimeMetricsSampler` (metrics, FPS slot, Synrei)      |
| Understand | `RuntimeMetricsSampler::assess()` → `make_result`        |
| Decide     | `DecisionEngine::evaluate`                               |
| Validate   | `PolicyExecutor` checks (capability, thermal, runtime)   |
| Execute    | Transaction Engine through trusted operations            |
| Verify     | Transaction read-back                                    |
| Restore    | DecisionEngine RESTORE → `PolicyExecutor` `finish()`     |

`jni/policy/LivePolicyController.*` only connects these. It adds no collector, thread,
persistence, bottleneck model or transaction framework.

## Live call path

```
SessionManager::tick (daemon loop, session active only)
  └ runtime_metrics tick  → RuntimeMetricsSampler::tick (new sample?)
  └ live_policy tick      → LivePolicyController::tick
        gates: enabled, session id matches, sampler running for this session,
               samples_taken increased (fresh evidence), capabilities present,
               recovery clean, game active, GameRuntime transaction not
               Preparing/Restoring/Failed
        inputs: assess(now) [in-session, const], newest Synrei snapshot,
                FPS window (shortfall = fps < target * 0.9), profile mode,
                executor transaction state
        DecisionEngine::evaluate → PolicyExecutor::execute
```

Each sample is evaluated at most once. An already applied action returns `AlreadyActive`
without a write. The session-final `BOTTLENECK_ASSESSED` result is not used and not changed.

## MITIGATE semantics (B-38)

MITIGATE = lower a ceiling one step. It is recommended only under a verified Synrei `safety`
with a verified, reversible control. Under Synrei `boost` a confirmed CPU/GPU bottleneck yields
BOOST (confirmed bottleneck, observed FPS shortfall, performance profile, thermal not safety,
verified trusted control, acceptable risk, rollback) or OBSERVE. It is never MITIGATE.

## Lifecycle and restore ordering

Begin: GameRuntime → SessionRecorder → RuntimeMetrics → live policy enabled.
End (exit, focus loss, switch, death, failure, daemon stop): live policy disabled → PolicyExecutor
transaction restored → sampling stopped → statistics finished → **GameRuntime restore last**.
The policy restore runs before GameRuntime's restore because `live_policy` is the last participant
and SessionManager ends participants in reverse order (tested in `live_policy_test`).

## Execution boundary

- Writes happen only through `PolicyExecutor` trusted operations (cpufreq `scaling_max_freq`,
  KGSL `max_gpuclk`, devfreq `max_freq`) and the Transaction Engine.
- There are no arbitrary paths, no shell, no capability re-verification, and no adaptive,
  learning, escalation or repeated tuning.
- The journal is at `/data/adb/.config/zairenkai/policy.journal`. It is replayed at daemon start,
  and an unclean replay blocks live policy.
- Failures (executor, observer, restore) never stop GameRuntime. A failed restore makes the
  session end unclean.

## Adaptive outcome evaluation (Phase 5)

Each fresh sample is also fed to `AdaptiveController` (`ADAPTIVE_OPTIMIZATION.md`):
- an intervention under evaluation is judged before anything new is decided;
- a ROLLBACK verdict executes a RESTORE through `PolicyExecutor`;
- BOOST and MITIGATE decisions pass an admission gate (baseline, no escalation, cooldown,
  hysteresis) before the executor runs.

Restore ordering at session end is unchanged.
