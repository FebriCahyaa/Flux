# Kernel Intelligence (Step 7 — observation only)

Status: **IN PROGRESS.** Module `jni/kernel/KernelIntelligence.*` (`flux::kernel`, NDK lib
`FluxKernel`, host lib `flux_kernel`). Linked into `fluxd`, **no call path yet**. Device: NOT_TESTED.

*Nothing is optimized before it is understood.* This layer answers four questions and nothing else:
what kernel is running, which interfaces exist, what can be read, what permissions suggest could be
written — and which interfaces need a vendor adapter.

## Guarantees

- **No writes.** `ReadOnlyFs` has no write operation. `writable` comes from `access(W_OK)` and is a
  hint; `verified` is always `false` until a later phase proves write + read-back through the
  Transaction Engine.
- No policy, no tweaks, no thermal control, no GameRuntime lifecycle change, no device-specific code.
- Legacy outputs (`kernel_type`, `is_gki` written by `service.sh` / `flux_profiler.sh`) are untouched;
  their readers keep working (B-05 is resolved in the model, the old files stay until migration).

## Identity and classification (B-05)

Evidence: `/proc/sys/kernel/osrelease` (preferred) and the `/proc/version` banner (fallback).
Two **independent** axes, each with a confidence and a reason string:

| Axis | Rule | Confidence |
|---|---|---|
| Generation | kernel < 4.19 → Legacy, else NonLegacy | High when the version parses, else Unknown/None |
| Integration | `-androidN-` tag with N ≥ 12 **and** kernel ≥ 5.10 → GKI | High (two agreeing signals) |
| | tag present but the other signal disagrees (e.g. `android11-5.4`) | Unknown / Low |
| | no tag, kernel ≥ 5.10 (custom/stripped build) | NonGKI / Medium |
| | no tag, kernel < 5.10 | NonGKI / High (GKI 2.0 impossible) |
| | release only from `/proc/version` | one step lower |

A single heuristic never produces a High GKI claim. `kmi` is reported as `androidN-X.Y`.

## Capability probing

Every adapter publishes `ProbeSpec`s (id template, domain, path with `*` segments, value kind, risk,
rollback, range source). The prober expands globs, reads each node once, validates it and produces a
`Capability` (see `CAPABILITY_MODEL.md`). Missing interfaces are reported as `supported=false`
records — a glob with no match gives one record with `*` in its id — so absence is visible, not
silent. Invalid nodes (directory or dangling link where a file belongs, empty, control bytes,
oversized > 4 KiB, not an integer, selector without one `[selected]` entry) are `supported=true,
readable=false`, confidence Low, with the reason in `note`.

Domains: CPUFreq, CPU policy, governor, uclamp, scheduler, cpuset, cgroup, DevFreq, GPU, thermal
(observe-only, risk High), ZRAM, swap, I/O scheduler (pseudo devices `loop`/`ram`/`zram`/`dm-`
skipped), input boost, display refresh (sysfs modes; the dumpsys-based panel rates stay in
`RuntimeHost::parse_panel_rates`).

## Adapters

| Adapter | Selected when | Adds |
|---|---|---|
| generic | always | all generic sysfs/procfs interfaces |
| qualcomm | `ro.hardware=qcom`, manufacturer QTI/Qualcomm, platform `msm/sdm/sm/qcs####`; and/or `kgsl-3d0` present | KGSL GPU, `cpu_boost` input boost |
| mediatek | platform/hardware `mt####`, manufacturer MediaTek/MTK; and/or `proc/gpufreq(v2)` present | gpufreq OPP tables, GED GPU load |

Platform strings are **full-match** patterns, never substrings (B-07: `*mt*`, `*sm*` misclassified).
Property and interface agreeing → High; one of them → Medium. The best vendor adapter adds its specs
(`requires_adapter=true`, `source=<adapter>`); others are not probed. New vendors implement `Adapter`
and register through `AdapterRegistry::add` — there is no other place for vendor knowledge.

## Not in this step

Writes, verification, tweak contracts, policy, daemon call path, Observatory KERNEL events, JSON
export, WebUI. B-28 (view-normalised journal entries): the `Selector` kind here is the shape a future
journal needs to record so `recover()` can compare like with like — decided in the adapter write phase.

## Tests

`tests/kernel_intelligence_test.cpp`: version parsing; GKI/NonGKI/Legacy classification; GKI
confidence from single or disagreeing signals; `/proc/version` fallback; capability probing on a fake
tree (values, ranges, selectors, observe-only thermal, all 15 domains present); missing interfaces;
invalid nodes; a real temp tree (directory as node, dangling symlink) with an unchanged-content/mtime
check; adapter selection incl. substring non-matches; a registered third-party adapter.

## Step 7.5 — Export to the shared capability context

`flux::kernel::export_facts(report)` / `publish(report, context)` put every capability, plus
`kernel.integration`, `kernel.generation` and `kernel.adapter`, into the shared capability context
under the publisher `kernel`. The export is 1:1 and nothing is upgraded: confidence, readable, writable,
verified (still always false), risk and source are copied, and an Unknown classification stays
Unknown. Rules and conflict handling are in `CAPABILITY_MODEL.md`.

Still nothing calls `observe()` inside `fluxd`, and writes remain disabled. The Performance Planner can
receive the context through `PerfCapabilities::context`, but none of its decisions read it yet, and a
test shows its plans are identical with and without the context.

## Step 7.6 — Runtime capability bootstrap

### Ownership

| Component | Owns | Does not |
|---|---|---|
| Kernel Intelligence (`observe`, adapters) | collecting kernel facts (read-only) | store state or decide anything |
| `CapabilityContext` | the normalised runtime capability state for the daemon's lifetime | probe, write, infer |
| `CapabilityBootstrap` (`jni/kernel/CapabilityBootstrap.*`) | moving one probe result into the context | keep its own copy or interpret facts |
| SynthesisCore / Aeyrin | future schema adaptation (schema v4, D-11) | — unchanged in this step |

### Lifecycle in fluxd

```
fluxd start
  -> flux_capability::bootstrap()        (jni/CapabilityHost.*, Main.cpp before boot recovery)
       -> make_device_probe("/", ro.board.platform / ro.hardware / ro.soc.manufacturer)
       -> observe() -> export_facts() -> context.publish("kernel", ...)
  -> flux_session::manager().recover()   (unchanged)
  -> engines read flux_capability::context()  (GameRuntime planner: PerfCapabilities::context)
```

The probe runs once at daemon start, on the main thread, before any profile script. It is not re-run on
a timer. `CapabilityBootstrap::run()` can be repeated (for example by a future refresh trigger), and each
run replaces the kernel snapshot.

### Failure behaviour

| Case | Status | Context | Daemon |
|---|---|---|---|
| probe OK, ≥1 capability supported | `ok` | kernel snapshot published | continues |
| probe OK, nothing supported | `empty` | absences published as No, identity Unknown | continues |
| probe throws (any type), no probe, no context | `failed` | nothing new published; on the first run every capability stays **Unknown** | continues; one warning in `flux.log` |
| later run fails after a good run | `failed` | last good snapshot kept, generation unchanged | continues |

`run()` never throws. The bootstrap observer (a hook for the future Observatory) receives the
`BootstrapResult`, and its exceptions are swallowed. It is not wired to anything: there is no kernel
event type, no persistence and no telemetry. `flux.log` gets one line per run.
