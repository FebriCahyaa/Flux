# Zairenkai Performance Planner

Step 2 of `GAME_RUNTIME_MIGRATION_PLAN.md` (branch `integration/game-runtime-clean`).
Code: `jni/perf/PerformancePlanner.{hpp,cpp}` (`flux::perf`) · Tests: `tests/performance_planner_test.cpp`.
Status: **Step 2 IN PROGRESS** — implemented and host-tested; linked into `fluxd` as `FluxPerf`;
**no daemon call path** (needs Session, Step 5); not device-tested.

## Responsibility split

```
PerfCapabilities (probe, read-only)   PerfProfile (per game)   GameContext
                 \                          |                     /
                  +------ PerformancePlanner (decides WHAT) -----+
                                     |
                 RuntimePlan(domain="performance")  +  refresh_target_hz  +  report
                                     |
                 Runtime Transaction Engine (decides HOW: snapshot, journal,
                 apply, verify, rollback, restore)
```

The planner never calls `Io::write`; it only constructs `NodeWriteOperation`s. Tests assert zero
writes during planning.

## Inputs

| Input | Content |
|---|---|
| `PerfCapabilities.node_available` | capability check: node exists and is readable (probe, never writes) |
| `PerfCapabilities.block_queues` | internal `/sys/block/<dev>/queue` dirs |
| `PerfCapabilities.panel_refresh_hz` | refresh modes the panel reports |
| `PerfProfile` | memory, touch, storage, refresh (+ custom Hz), launch_boost |
| `GameContext` | package (plan subject) |
| mitigation gate | per-category device-mitigation decision |

Kernel capability beyond node presence (Integration × Generation, writable-verified) arrives with
Kernel Intelligence (Phase 3) through the same `node_available` seam.

## Per-action gates

Every planned write passes, in order:
1. **Selection validity** — unknown level strings are errors; nothing is guessed.
2. **Mitigation** — a blocked category plans nothing and is reported `blocked_by_mitigation`.
3. **Interface check** — `interface_allowed()`: only `/proc/sys/vm/{swappiness,vfs_cache_pressure,
   page-cluster,dirty_expire_centisecs,dirty_writeback_centisecs,watermark_boost_factor}`,
   `/sys/module/cpu_boost/parameters/input_boost_ms`, `/dev/cpuctl/top-app/cpu.uclamp.min`,
   `/sys/block/<dev>/queue/{read_ahead_kb,rq_affinity}`; no `..`.
4. **Capability check** — node available on this device; missing nodes are counted, not planned.
5. **Verification support** — each operation is a `NodeWriteOperation` (read-back verify,
   snapshot restore).

## Levels (unchanged from the old branch values)

| Category | Level → writes |
|---|---|
| memory | balanced: vfs_cache_pressure 100, page-cluster 0 · gaming: swappiness 60, vfs_cache_pressure 80, page-cluster 0, dirty_expire 1500, watermark_boost 0 · gaming_plus: swappiness 40, vfs_cache_pressure 60, page-cluster 0, dirty_expire 3000, dirty_writeback 3000, watermark_boost 0 |
| touch | input_boost_ms 40 / 80 / 120 / 160 (balanced / responsive / responsive_plus / competitive) |
| storage | read_ahead_kb 256 (balanced) / 512 + rq_affinity 2 (gaming) per queue |
| launch boost | read_ahead_kb 1024 per queue, top-app uclamp.min 50 |
| refresh | `refresh_target_hz` only if the panel reports that rate; `real` and `adaptive` request nothing. Applied by `flux_refresh` in the profile script (single refresh writer) — wiring in a later step. |

## LaunchBoost

| Property | Implementation |
|---|---|
| Bounded | duration clamped to 1–15 s (default 8 s); `tick(now)` restores at the deadline |
| Cancelable | `cancel(MainActive / Timeout / ProcessExit / ProfileChange / Watchdog)`, idempotent |
| Snapshot-protected | runs as one `Transaction` with write-ahead journal |
| Rollback | failed write → transaction rolled back, `begin()` returns false |
| One at a time | a second `begin()` while boosting is refused |
| Never sleeps | driven by the caller's existing loop |

## Tests (written before implementation; red → green)

profile→plan conversion, zero writes while planning, default = empty plan, invalid selections
rejected, unsupported nodes, unsupported refresh rate, mitigation block, interface rejection
(including `..`), plan executed through a Transaction and restored, transaction failure rollback,
launch-boost timeout, duration clamping, one-at-a-time, cancellation + journal emptied,
nothing-available, failing write rollback.

## Removed / not ported

No `Resolver`, `ProviderPlan`, `Arming`, `Zygisk`, identity profiles or `compat` settings; the old
`build_actions(Io, PerfPlanInput)` probed with `Io::exists` inside the planner — replaced by the
read-only `PerfCapabilities` seam.

## Known limitations

- Not called by `fluxd` yet; daemon wiring waits for Session (Step 5) so start/stop has an owner.
- The capability probe that fills `PerfCapabilities` on the device is not written yet.
- `input_boost_ms` exists only on Qualcomm `cpu_boost` kernels; other input-boost interfaces
  arrive with kernel adapters (Phase 3).
- Refresh target is computed but not yet passed to `flux_refresh`.
