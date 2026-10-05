# Runtime Metrics Collector (Step 8.7 — read-only)

Status: **IN PROGRESS.** Module `jni/metrics/RuntimeMetrics.*` (`flux::metrics`, NDK `FluxMetrics`, host
`flux_metrics`). Linked into fluxd, **no call path**: nothing samples during a session yet (B-33).
Device: NOT_TESTED.

It reads procfs/sysfs through `flux::kernel::ReadOnlyFs`, which has no write operation. There is no
optimisation or policy, and kernel nodes, GPU behaviour and thermal behaviour are never changed. Its
only consumer is the BottleneckModel input (`to_runtime_sample`).

## Metric record

Every `Metric` has `value` (numeric) or `text`, a `unit`, a `timestamp_ms`, a `source` (node path or
`derived:<ids>`), a `confidence` (HIGH/MEDIUM/LOW/UNKNOWN), `readable`, `verified` and a `note`.

- `readable`: a value was read **and** parsed.
- `verified`: the value was cross-checked, either because a second source agrees or because it lies
  inside a declared range (`cpuinfo_min_freq..cpuinfo_max_freq`). A single unchecked source is never
  verified.

**Missing data is UNKNOWN:** `readable=false`, no value, confidence UNKNOWN, and a note with the reason.
The reason is one of:
- the node is missing;
- the node is unreadable (permission or I/O);
- the content is malformed;
- the metric needs a previous sample;
- there was no interval between samples.

Nothing is inferred. MemFree is never used in place of MemAvailable, and IO latency is never estimated
without completed requests.

## Metrics

| Id | Source | Notes |
|---|---|---|
| `cpu.utilization.busiest_core` (`text` = core), `cpu.utilization.total` | `/proc/stat` deltas | UNKNOWN on the first sample |
| `cpu.cluster.<policy>.cpus`, `.cur_mhz`, `.max_mhz` | cpufreq policy nodes | cur: `cpuinfo_cur_freq` (hardware) preferred, `scaling_cur_freq` otherwise |
| `cpu.freq_ratio.busiest_cluster` (`text` = policy) | derived | cluster that contains the busiest core |
| `gpu.interfaces` | presence of kgsl, devfreq gpu/mali, mali0, ged | |
| `gpu.utilization` (%) | kgsl `gpu_busy_percentage` > kgsl `gpubusy` > ged `gpu_utilization` > devfreq `load` | first readable answers, second cross-checks |
| `gpu.freq.cur_mhz`, `gpu.freq.max_mhz`, `gpu.freq_ratio` | kgsl `gpuclk`/`max_gpuclk`, else devfreq | |
| `mem.total_mb`, `mem.available_mb`, `swap.total_mb`, `swap.free_mb` | `/proc/meminfo` | |
| `mem.psi_some_avg10`, `mem.psi_full_avg10` | `/proc/pressure/memory` | |
| `zram.<dev>.orig_mb`, `.compr_mb` | `sys/block/zram*/mm_stat` | |
| `io.psi_some_avg10` | `/proc/pressure/io` | |
| `io.iowait_ratio` | `/proc/stat` deltas | |
| `io.<dev>.scheduler` | kernel context fact `io.<dev>.scheduler`, else the node | pseudo devices skipped |
| `io.<dev>.latency_ms` | `sys/block/<dev>/stat` deltas (ticks ÷ completed reads+writes) | Medium; UNKNOWN without completed I/O |

## Conflicting sources

| Metric | Agree | Disagree |
|---|---|---|
| CPU cluster frequency | within 10 % → hardware value, HIGH, verified | hardware value kept, MEDIUM, not verified, note `conflict:` with both values |
| GPU utilisation | within 15 points → first source, HIGH, verified | first-priority source kept, MEDIUM, not verified, note `conflict:` |

## Bottleneck input

`to_runtime_sample(snapshot, fps, target_hz)` maps readable metrics only:
- busiest core → `cpu_busiest_core`;
- busiest cluster ratio → `cpu_freq_ratio`;
- GPU % ÷ 100 → `gpu_busy`;
- `gpu_freq_ratio`, memory PSI and available memory, IO PSI → the matching fields.

UNKNOWN stays `nullopt`. `fps` and `target_hz` come from the session (SessionRecorder), never from this
collector.

## Tests

`tests/runtime_metrics_test.cpp` covers:
- collector success: two samples with values, sources, timestamps and confidence;
- missing interface: everything UNKNOWN, and MemAvailable absent is not derived;
- malformed values;
- permission denied;
- conflicting metric sources: CPU frequency and GPU busy, with agreement giving verified;
- bottleneck input: a CPU-bound series from collected samples assesses as CPU;
- read-only: the tree is unchanged after sampling.
