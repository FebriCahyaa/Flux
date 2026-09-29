# Migration Matrix — Flux / SynthesisCore / HiCo → Zairenkai / Aeyrin / Synrei

Phase 0 deliverable. One row per subsystem: where it is today, where it goes, and how.
Derived from `ZAIRENKAI_BASELINE.md`; no row authorises a deletion (see `DELETION_MANIFEST.md`).

| Status | Meaning |
|---|---|
| REUSABLE | Keep as is; wrap or rename later. |
| MODIFY | Keep and extend in place. |
| REPLACE | A new implementation takes over **after** the old one is migrated, validated and deprecated. The old one stays until then. |
| DEPRECATE | Keep working behind a compatibility path; removal needs a manifest entry. |
| UNKNOWN | Not enough evidence; do not touch. |
| NEW | No current implementation; nothing to migrate. |

## Identity and contracts

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| Module id `flux`, `/data/adb/modules/flux` | Zairenkai module | UNKNOWN | 13 | Changing the id orphans installed modules, update channels and HiCo's `FLUX_MODULE_DIR`. Needs a migration design (dual id or installer handover) before any change. |
| Module id `hico` | Synrei module | UNKNOWN | 13 | Separate release repo (`HiCo-Release`) and proprietary license. |
| `/data/adb/.config/flux/` | `/data/adb/.config/zairenkai/` | UNKNOWN | 13 | User config must migrate, not reset. Telemetry tree (Phase 5) is new and can start under the new name. |
| `current_profile`, `gameinfo`, `.lock`, `synthesis_core.json` | Same files, same format | REUSABLE (frozen) | — | Consumed by HiCo; any change needs a versioned successor published **alongside**. |
| `update*.json`, `update/changelog.md` | Same paths until a channel migration exists | REUSABLE (frozen) | 15 | Installed devices poll these URLs. |
| `FluxProfileMode` integers | Same values | REUSABLE (frozen) | — | Mirrored by HiCo `FluxProfile`. |
| Package `com.febricahyaa.synthesiscore`, `MainKt` entry | Aeyrin | UNKNOWN | 13 | `service.sh` invokes it by class name; certificate pin in Flux. |
| Brand strings "Flux Tweaks", "HiCo Thermal", "SynthesisCore" | Zairenkai, Synrei, Aeyrin | DEPRECATE (later) | 1 → 13 | Only after the naming clearance audit (Phase 1). |

## Aeyrin (context and capability)

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| SynthesisCore providers (foreground, display, power, battery, thermal, audio, kernel) | Aeyrin Android/Runtime State context | REUSABLE | 2 | Event engine, atomic writer and hardening are sound. |
| Protocol v3 status file | Aeyrin context protocol (v4+) | MODIFY | 2 | Additive only; v3 readers must keep working (`synthesis_core_test` proves this today). |
| `KernelProvider` (`-android\d+-`) | Aeyrin Kernel context fed by Kernel Intelligence | REPLACE | 3 | Single weak heuristic today. |
| Capability model v4 (Flux `jni/gfx`, SynthesisCore unmerged) | Aeyrin Capability Graph | MODIFY | 2 | Schema ownership must be settled first (B-03). Model already stores provenance and treats absent as unsupported. |
| `DeviceInfo`, `PlatformProbe` | Aeyrin Device context | MODIFY | 2 | Exists but does not drive eligibility. |
| `NativeMonitor` + `BinderNDK` | Aeyrin native collector | REUSABLE | 2 | Keep the Java fallback. |

## Zairenkai Kernel

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| `detect_kernel_type` (profiler), GKI block in `service.sh` | Kernel Intelligence Engine: Integration × Generation axes, confidence | REPLACE | 3 | Old writers keep writing `is_gki`/`kernel_type` until consumers move; new result published in a new file. |
| Per-write "node exists" checks | Read-only capability probing + verified-writable marking | MODIFY | 3 | |
| SoC branches in `flux_profiler.sh` | Kernel adapters (Generic, Qualcomm, MediaTek, Samsung, Xiaomi, …) | MODIFY | 3 | Adapters translate; no policy in adapters. |
| `soc_recognition` integer | Capability-based device classification | DEPRECATE | 2–3 | Two decoding tables exist (B-07); fix the table before relying on it. |

## Zairenkai Performance

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| `flux_profiler.sh` profiles | Performance engine executing tweak contracts | MODIFY | 3, 8, 12 | Largest single asset; evolve function by function behind contracts. |
| `apply()` (write + chmod 444, no read-back) | Transactional apply: snapshot → apply → verify → commit / rollback | REPLACE | 11 | Keep `apply()` for untouched call sites until each migrates. |
| Flux Sched, Flux Boost, priority, cgroup, chipset save/restore | Tweak contracts with rollback | MODIFY | 3, 11 | Already snapshot/restore; add verification and journaling. |
| `perfcommon` boot writes | Contracts with eligibility | MODIFY | 3, 12 | No rollback today. |
| Device mitigation (`device_mitigation.json`) | Eligibility "unsupported conditions" | MODIFY | 3 | Data preserved; schema extended, not replaced. |

## Zairenkai Graphics

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| `VulkanProbe`, capability model gfx facts | GPU Capability | REUSABLE | 7 | |
| GPU governor per profile, KGSL/Adreno props, Mali policy nodes | GPU Adapter + GPU Policy | MODIFY | 7 | |
| `RefreshHold` (launcher), `flux_refresh`, `flux_adaptive_refresh` | Refresh Strategy | MODIFY | 7 | |
| `RefreshMatcher` inside `SessionRecorder` | Refresh Strategy (policy) fed by Observatory (facts) | MODIFY | 6–7 | Today an observer makes refresh decisions; split ownership. |
| `RenderBooster` | Render thread placement (Performance/Graphics boundary) | REUSABLE | 7 | |
| FPS sources in `SessionRecorder` | Frame pacing observation | MODIFY | 7 | Keep FPS and refresh rate as separate facts (already separated in UI). |
| — | Render scaling / true upscaling | NEW | 7 | Capability-gated; never advertised without proof. |

## Zairenkai Runtime and Compatibility

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| Game detection, PID tracker, 3-strike focus, DND handling (`Main.cpp`) | Runtime session lifecycle | REUSABLE | 8 | |
| Game Runtime / Compatibility Engine / PerfPlanner (unmerged branch) | Zairenkai Runtime + Compatibility | UNKNOWN | 8 | Must be merged by its owner first (B-01); identity layer needs a policy decision (B-02). |
| Zygisk provider (unmerged) | Optional compatibility backend | UNKNOWN | 8 | Off by default; process-scoped. Same blockers. |
| Per-game `lite_mode`, `enable_dnd` in `gamelist.json` | GLOBAL → PRESET → GAME → RUNTIME profile hierarchy | MODIFY | 8 | Existing `gamelist.json` must load unchanged. |

## Zairenkai Observatory

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| `SessionRecorder` summary math (`session_summarize`) | Observatory session analysis | REUSABLE | 4–6 | Pure function; testable. |
| `session_live.json`, `sessions.json` (30 sessions) | Session store under `telemetry/sessions/…` with 7-day retention | REPLACE | 5 | Old files keep being written until the WebUI reads the new store; then DEPRECATE. |
| spdlog `flux.log` | Structured events + human log | MODIFY | 4 | Keep `flux.log` for bug reports. |
| `flux_utility save_logs` | Diagnostics export (retained data only) | MODIFY | 5, 10 | |
| — | Event model, timeline, decisions, explanations, installation epoch, index | NEW | 4–6 | |

## Synrei (thermal)

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| `hicod` controller, backends, journal, SafetyGuard, HeadroomGuard | Synrei Thermal Controller / Safety / Adapters | REUSABLE | 9 | Keep journaled restore and guards. |
| Flux thermal tiering (headroom/status/CPU °C in `Main.cpp`) | Consumer of Synrei thermal state | MODIFY | 9 | Flux must not contradict Synrei; today both decide independently. |
| Flux `step_wise` at boot when HiCo absent | Synrei (or leave vendor default) | UNKNOWN | 9 | Ownership question when Synrei is not installed. |
| HiCo Max / Extreme levels | Synrei policy under a hard safety ceiling | UNKNOWN | 9 | Evaluate against directive §59 (B-04). |
| HiCo thermal database (`stock/`, `database/`, `thermal-data/`, …) | Synrei thermal knowledge base | REUSABLE (protected) | — | Never regenerated-over; see HiCo inventory. |

## WebUI

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| Vue shell, router, Material 3 Expressive, locales | Same shell | REUSABLE | 10 | Do not recreate. |
| Monitor + SessionDetail views | Observatory (Live, Session, Timeline, Analysis, System, Kernel, Log) | MODIFY | 10 | Built around existing components. |
| `flux_utility` exec bridge (`$@` dispatch) | Allowlisted command bridge | MODIFY | 11 | Security gap today. |

## Build, CI, release

| Current | Target | Status | Phase | Notes |
|---|---|---|---|---|
| ndk-build (device), CMake (host tests) | Same | REUSABLE | — | Directive §52. |
| `build-module` action, three flavors, SHA-256 | Same + artifact safety checks | MODIFY | 15 | |
| SynthesisCore sync with checksum + cert + attestation | Same for Aeyrin | REUSABLE | 13 | |
| Version `1.4.1`, Conventional Commits changelog | Generation + stellar codename metadata | MODIFY | 1, 15 | No reset of version history. |
