# Capability Verification Foundation (Step 8.14)

Status: **IN PROGRESS** (implementation complete on the host). `jni/kernel/CapabilityVerification.*`, fluxd wiring in
`jni/CapabilityHost.cpp` (`flux_capability::verify()`, called from `Main.cpp`). **Device: NOT_TESTED. On real devices,
verified=true has not been observed yet.**

## supported ≠ readable ≠ writable ≠ verified

These four properties are distinct:
- **supported**: the interface exists;
- **readable**: its value was read and parsed;
- **writable**: a permission hint only;
- **verified**: proven by a transactional write and read-back cycle.

A writable capability is **not** automatically verified. Verified is set only after:

```
read current → validate → snapshot → safe reversible test value → write (through the Transaction Engine)
→ exact read-back of the test value → restore original → exact final read-back of the original
```

It is never set merely because the file exists, permissions allow writing, `write()` returned success,
or the current value was readable.

## Architecture

```
KernelIntelligence → CapabilityContext → CapabilityVerifier → VerificationResult
                                                         → apply_verification → CapabilityContext (verified=true)
                                                                              → DecisionEngine may consume later
```

The verifier is independent of the Decision Engine. It does not call it, decide, or execute
MITIGATE, BOOST or RESTORE. There is no Policy Executor, and GameRuntime, PerformancePlanner, Synrei
and the thermal state are untouched.

## Explicit verifier adapters (the only writers)

| Adapter | Exact interface pattern (relative, full match) | Safe reversible test value |
|---|---|---|
| `cpufreq_scaling_max_freq` | `sys/devices/system/cpu/cpufreq/policyN/scaling_max_freq` | largest `scaling_available_frequencies` entry **below** the current value and ≥ `scaling_min_freq` (lowering a ceiling) |
| `kgsl_max_gpuclk` | `sys/class/kgsl/kgsl-3d0/max_gpuclk` | largest `gpu_available_frequencies` entry below the current value |
| `devfreq_max_freq` | `sys/class/devfreq/<name>/max_freq` | largest `available_frequencies` entry below the current value and ≥ `min_freq` |
| `block_read_ahead_kb` | `sys/block/<dev>/queue/read_ahead_kb` | half the current value (+8 KiB when below 16) |
| `vm_swappiness` | `proc/sys/vm/swappiness` | current − 1 (or 1 from 0) |

If there is no frequency list, or no lower entry, the result is `no_safe_test_value` and nothing is
guessed.

**Without an adapter the capability stays `verified=false`, reason `no_safe_verifier`:**
- `scaling_min_freq`;
- governor (switching can reset governor tunables, so it isn't exactly reversible);
- uclamp, cpuset, scheduler sysctls;
- devfreq and KGSL governors;
- the I/O scheduler selector (also B-28);
- zram;
- input boost;
- everything display, graphics and rendering.

The verifier never derives write semantics from a path name. Only these five classes exist, and
adding one is an explicit, reviewed change.

## Gating (checked in order, before any I/O)

| Condition | Result (attempted = false) |
|---|---|
| thermal (domain `thermal`, an id `thermal.*`, or an interface containing `thermal` or `cooling_device`) | `thermal_protected`: **never written** |
| support ≠ Yes | `unsupported` |
| not readable | `unreadable` |
| not writable | `not_writable` |
| no rollback | `no_rollback` |
| risk Unknown / High | `unknown_risk` / `high_risk` |
| interface empty, absolute, containing `..` or `//`, or with characters outside `[A-Za-z0-9_./:,+-]` | `unsafe_interface` |
| no adapter pattern matches | `no_safe_verifier` |
| precondition refuses (fluxd: Synrei state `boost`, `relaxed` or `safety`, verified and fresh) | `precondition: …` |
| node missing or unreadable now / current value not an integer | `unreadable` / `invalid_current_value` |
| no safe test value | `no_safe_test_value` |

The path is always `"/" + interface`, built by the verifier. A caller-supplied path is never used.
The device I/O is the existing `make_node_io`: it never creates nodes, and it follows sysfs links,
which is the existing trusted policy for these fixed sysfs locations.

## Transaction and recovery semantics (existing engine, no second framework)

Each verification is one `RuntimePlan` (domain `capability_verification`, subject = capability id)
holding a single `NodeWriteOperation`, run by `Transaction`:
- **start:** snapshot, write-ahead journal, write the test value, exact trimmed read-back. If the
  read-back fails, the engine rolls back and decides restoration by read-back.
- **finish:** restore the original and decide restoration by read-back.

| Failure | Result |
|---|---|
| journal cannot be written | `journal_failed`, nothing written |
| write rejected | `write_rejected`, attempted, rolled back (restored by read-back) |
| write accepted but read-back differs | `readback_mismatch`, `readback_value` recorded, rolled back |
| restore write rejected | `restore_write_rejected`, `restored=false`, journal **kept** |
| restore read-back differs | `restore_readback_mismatch`, `restored=false`, journal kept |

In fluxd the journal is `/data/adb/.config/zairenkai/verification.journal` (atomic writes; removed
when empty). At the next daemon start, a leftover journal is replayed with the existing
`runtime::recover()` before any new verification. If that replay is not clean, verification is
skipped for that run.

## Lifecycle

`flux_capability::verify()` runs **once per daemon start**:
1. Observatory start;
2. capability bootstrap (KernelIntelligence → CapabilityContext);
3. GameRuntime boot recovery;
4. **verification**;
5. profile scripts.

There is no verification per sample, per tick or per game launch. Within one verifier lifetime each
capability id is attempted at most once; repeated calls return the cached result without I/O.

## VerificationResult

Fields: `capability_id`, `verifier`, `attempted`, `verified`, `before_value`, `test_value`,
`readback_value`, `restored`, `final_value`, `confidence` (HIGH only when verified), `risk`,
`reason`. Results are logged and **not persisted to telemetry**: there is no storage contract, the
event schema is unchanged, and transient test values are not persisted.

## CapabilityContext update

`apply_verification(context, results)` republishes the kernel publisher's snapshot with
`verified=true` set only on fully verified ids, plus a note naming the adapter. Every other field
(supported, readable, writable, interface, source, confidence, risk, rollback, range, value, adapter
and so on) and every other fact or publisher is preserved. Failed results change nothing.

## Decision Engine compatibility

`flux::policy::gate()` already treats verified + writable + readable + supported + rollback with
acceptable risk as actionable. After verification, the matching resource gate can become actionable.
The Decision Engine itself is unchanged: same action types, hierarchy, evidence rules, thresholds,
thermal and profile semantics. It still has no call path and no executor.

## Tests

`tests/capability_verification_test.cpp` (5 functions) covers the 24 required cases:
- gating: unsupported, unreadable, non-writable, unknown and high risk, no rollback, missing verifier,
  vanished node;
- success and preservation: all five adapters, original value preserved, exactly two writes,
  write-ahead journal and empty-after-restore journal, metadata preserved, only the verified id
  changed, the Decision Engine gate becoming actionable;
- failures: write rejected, read-back mismatch, restore rejected (journal kept), restore mismatch, no
  safe test value (lowest frequency, no list), invalid current value, journal failure, precondition;
- no repeated writes, thermal never written, unsafe or arbitrary interfaces refused;
- determinism, writes confined to the verified targets, verification alone not changing the context,
  failures not marking verified, Observatory registry and schema unchanged.

## Consumed by the Policy Executor (Phase 4B)

The executor's trusted operations (cpufreq `scaling_max_freq`, KGSL `max_gpuclk`, devfreq `max_freq`)
execute only on facts with `verified=true`; anything else is `capability_unverified`. Until device
validation (B-37) produces real verified facts, the executor blocks on devices.

## Live policy (Phase 4C)

The live policy loop does not re-run verification. It consumes the facts that the single
start-up verification published; an unverified control keeps blocking execution
(`capability_unverified`).
