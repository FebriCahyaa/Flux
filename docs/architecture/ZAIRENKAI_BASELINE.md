# Zairenkai Baseline — Forensic Architecture Audit (Phase 0)

What the ecosystem **is** today, measured from code, before any renaming or new subsystem.
Nothing in this document is a plan; the plan is in `docs/agent-state/PLAN.md` and the per-subsystem
decisions are in `MIGRATION_MATRIX.md`.

## 1. Repositories and commits audited

| Repository | Role today | Default branch @ audited commit | License | Notes |
|---|---|---|---|---|
| `FebriCahyaa/Flux` | Performance module, `fluxd` daemon, WebUI | `main` @ `b75491c` | Apache-2.0 (derived from Encore Tweaks) | 299 tracked files |
| `FebriCahyaa/SynthesisCore` | System-state monitor APK (`app_process`), binder code resolver | `master` @ `7cbdda2` | Apache-2.0 | 58 tracked files; releases v2.1.0, v2.1.1 |
| `FebriCahyaa/HiCo` | HiCo Thermal module (`hicod`), thermal knowledge database | `main` @ `a61cad0` | **Proprietary (EULA.md)** | 45,453 tracked files (~211 MB) |

All three clones are shallow; the working trees were clean. Work branch in each:
`ccr-0dc934d1-0a6zta`, created from the default branch above.

### Unmerged branches (inspected, not merged, not deleted)

| Repo | Branch | Head | vs default | Content |
|---|---|---|---|---|
| Flux | `ccr-23925ebb-375wkg` | `f811dad` | +11 / −0 | **Game Runtime, Compatibility Engine, Zygisk provider** (74 files, +11,002 lines) |
| Flux | `sync/synthesiscore-v2.1.0` | `676b80c` | +1 / −89 | Superseded APK sync (main pins v2.1.1) |
| SynthesisCore | `claude/hico-thermal-game-9ree3l` | `5eb79f6` | +1 / −1 | **Canonical capability model, protocol 4** (`schema/capability_schema_v4.json`) |
| SynthesisCore | `claude/feature-system-adjustment-04m3t9` | `02023f0` | +0 | merged |
| SynthesisCore | 4 × `dependabot/*` | — | — | dependency bumps, not inspected in detail |
| HiCo | 9 branches (`backup/*`, `ccr-79d053f0-*`, `claude/*`, `devices/*`, `phase-2.5d-*`, `stock/ingest-*`) | — | +0 each | all merged into `main` |

Consequence: several subsystems the Zairenkai directive calls "existing" (Game Runtime,
Compatibility Engine, transaction/rollback journal, optional Zygisk provider, SynthesisCore-owned
capability schema) **exist only on unmerged branches**. See BLOCKERS B-01 and B-03.

## 2. Runtime architecture on `main`

```
                         boot (service.sh)
                               │
   synthesiscore.apk --resolve │  (checksum re-verified first)
        → binder_codes          ▼
                         fluxd daemon  ──────────────────────────────┐
  NativeMonitor (binder observers) ─┐                                │
   or SynthesisCore Java daemon ────┴─► synthesis_core.json (v3 k/v) │
                               │ inotify → eventfd → main poll loop   │
                               ▼                                      │
           select_profile(): game tier → audio hold → powersave → balance
                               │                                      │
             Profiler.cpp ─► flux_profiler.sh <profile> (sysfs/procfs writes)
                               │                                      │
    SessionRecorder (1 Hz: FPS, CPU/battery °C, lite) + RefreshMatcher │
    RenderBooster (render threads → big cores, every 3 s)             │
    RefreshHold (launcher peak refresh outside games)                 │
                               │                                      │
       writes current_profile / gameinfo ─────────► HiCo hicod (thermal layer)
                               │
                  WebUI (Vue, KernelSU/WebUI X exec bridge)
```

### 2.1 Daemon lifecycle (`jni/Main.cpp`)
1. Root check → `flux_cli` dispatch (`daemon`, `setup_gamelist`, `check_gamelist`,
   `capabilities`, `version`).
2. `run_daemon`: refuses a second instance (lock), refuses while a module update is pending,
   requires parsable `gamelist.json` and `device_mitigation.json` (fatal otherwise), daemonises,
   takes `.lock`, creates one eventfd.
3. Starts `NativeMonitor` (binder); falls back to the SynthesisCore Java daemon, waiting up to
   120 s for its lock and stopping itself if the Java daemon's lock is released.
4. Main loop is fully event-driven (`poll` on one eventfd): inotify updates, PID-death callbacks,
   config changes, stop requests.
5. Profile selection priority: active game + screen awake → Performance / Performance Lite
   (thermal tiering, 5 s debounce, headroom 0.20/0.35 hysteresis, thermal status ≥ severe, CPU
   90 °C / 82 °C) → audio hold → battery saver → balance.
6. Game exit: PID tracker callback or 3-strike focus loss → stop session workers, restore DND.

### 2.2 Execution layer (`scripts/flux_profiler.sh`, 2,339 lines)
- Profiles: `perfcommon` (boot), `performance [lite]`, `balance`, `powersave`, `system`.
- SoC branches: MediaTek, Snapdragon, Tegra, Exynos, Unisoc, Tensor (from `soc_recognition`).
- Kernel branches: `apply_gki` / `apply_non_gki` / legacy notes (see §5).
- Writes go through `apply()` (write then `chmod 444`) or `write()`; **no read-back
  verification**. Some features snapshot and restore (Flux Sched uclamp, Flux Boost VM/I/O,
  priorities, cgroups, chipset, adaptive refresh, props, zram); most `perfcommon` writes have no
  rollback record.
- Leaves thermal zone policies alone when HiCo is active (`hico_active`), otherwise sets every
  zone to `step_wise` at boot.

### 2.3 Monitoring and telemetry today
| Signal | Source | Rate | Storage |
|---|---|---|---|
| Foreground app/PID/UID, screen, battery saver, DND, charging, audio, call, thermal headroom/status, battery level/temp | NativeMonitor (binder) or SynthesisCore | event-driven + safety polls | `synthesis_core.json` (overwritten) |
| FPS (FPSGO / Qualcomm measured / SurfaceFlinger page flips), hottest CPU zone, battery °C, lite flag | SessionRecorder | 1 Hz during games, paused screen-off | `session_live.json`; `sessions.json` keeps **30 sessions** |
| Daemon log | spdlog | event | `flux.log`, 2 MB rotation + previous boot copy |
| Capability facts (Vulkan, platform) | `fluxd capabilities` | on demand | `capabilities.json` (schema v4) |

There is no event timeline, no decision log with reasons/evidence, no transaction IDs, no
installation epoch and no time-based retention.

### 2.4 WebUI (`webui/`)
Vue 3 + Vite, built with Bun, hash-mode router (`/`, `/games`, `/settings`, `/monitor`,
`/monitor/session/:id`, `/games/:packageName`, 14 `/settings/*` pages), Material 3 Expressive, 10 locales, KernelSU / WebUI X
exec bridge. Monitor tab shows live session and history from the files above. The WebUI writes
`config.json` / `gamelist.json` directly and calls `flux_utility` with arguments (governor names are
validated against `[\w-]`).

### 2.5 Build, CI and release
- Device: `ndk-build` (NDK r29), C++23, `-O3 -flto`, arm64-v8a + armeabi-v7a.
- Host tests: CMake (C++20) — 7 tests with injection seams (no device paths).
- CI `build.yml`: host tests job + flashable zip job (three flavors: arm64, arm, universal, each
  with its own `update*.json`), SHA-256 for every file, Telegram notify.
- `release.yml`: builds, attaches zips + SHA-256, generates changelog, commits update channels.
- `sync_synthesiscore.yml`: syncs a SynthesisCore release only if checksum, pinned signing
  certificate and build-provenance attestation all verify.

### 2.6 Security and integrity (present today)
- Install: every extracted file verified against SHA-256; ABI/flavor gate.
- Boot: SynthesisCore APK checksum re-verified before each run as root.
- Singleton lock; `module.prop` restored from `.orig`.
- WebUI governor strings validated before `exec`.
- Gaps (recorded, not fixed in Phase 0): `flux_utility.sh` ends in `$@` (any shell function can be
  invoked by name); profiler writes are not allowlisted; no config schema validation beyond JSON
  parsing; no rollback journal for `perfcommon`.

## 3. Unmerged Game Runtime line (`Flux@ccr-23925ebb-375wkg`)
Inspected read-only. Not part of `main`.

| Component | Files | What it does |
|---|---|---|
| Compatibility Engine | `jni/compat/{Resolver,CompatTypes,Hardware,GameProfile}` | Resolves per-game requirements against the capability model |
| Game Runtime | `jni/compat/{GameRuntime,Runtime,Session}` | `activate_compat` → `activate_perf` → `reassert_perf` → `deactivate`; persisted rollback journal `compat_journal`, `compat_status.json` |
| Perf planner | `jni/compat/PerfPlanner` | Per-game memory/touch/storage overrides applied after the profile script |
| Provider plan | `jni/compat/{ProviderPlan,ProviderPaths,Arming,ZygiskBackend}` | Daemon writes per-package plans; arming list for the Zygisk module |
| Zygisk provider | `jni/zygisk/{FluxCompatModule,GotHook,Interpose}` | Process-scoped GOT hooks for `__system_property_get`, `glGetString`, `eglQueryString`, `vkGetPhysicalDeviceProperties(2)`, `vkGetInstanceProcAddr`, `dlopen` |
| Validation | `scripts/flux_runtime_validate.sh`, `scripts/flux_provider_validate.sh`, `tools/compat-testapp/` | Real-device checks |
| Tests | 10 host test files (~3,200 lines) | compat, session, provider, GOT hook, interpose, Zygisk backend, companion |

Safety properties observed: provider ships **off** (`zygisk_provider/`, opt-in copy to
`zygisk/`), skips isolated/system/webview processes, has a kill-switch file, bounded IPC, no
self-hiding (`DLCLOSE_MODULE_LIBRARY` only; the `FORCE_DENYLIST_UNMOUNT` match is the stock
Zygisk API header). What it changes: the **device / CPU / GPU identity** a single game process
reads (ro.* properties, Build fields, GL vendor/renderer/version, Vulkan device name, vendor/device
ID, driver and API version, with API version clamping). Whether identity substitution is inside the
directive's "legitimate process-scoped compatibility" and outside "fabricating hardware
capabilities" is an owner decision — BLOCKERS B-02.

## 4. SynthesisCore (future Aeyrin) today
- Kotlin, runs through `app_process` as root; not installed as an app. Android 9–17.
- Single-Looper engine, providers: foreground, display, power, battery, thermal, audio, kernel.
  Event-driven where the framework allows, adaptive polls otherwise, 25 ms write coalescing.
- Output: `synthesis_version 3` plain-text key/value, atomic `O_EXCL|O_NOFOLLOW` temp + fsync +
  rename. Unknown keys ignored; absent key = unsupported.
- `--resolve` binder transaction resolver used by Flux's native monitor every boot.
- Release chain: R8, signed, SHA-256 + certificate digest + provenance attestation; Flux pins both.
- Protocol 4 / canonical capability model exists only on an unmerged branch (BLOCKERS B-03).

## 5. Kernel handling today

| Where | Rule | Output |
|---|---|---|
| SynthesisCore `KernelProvider` | `-android\d+-` in `os.version` | `kernel_is_gki 0/1` |
| Flux `module/service.sh` | `-android[0-9]+-` in `uname -r` | `kernel_type` = `GKI`/`Non-GKI`, `is_gki` |
| Flux `scripts/flux_profiler.sh` | version ≥ 5.10 **and** `-android(12+)-` → `gki`; version < 4.19 → `legacy`; else `non_gki` | overwrites `kernel_type` = `gki`/`non_gki`/`legacy`, `is_gki` |

Findings: three classifiers disagree (a `5.4.x-android11-…` kernel is GKI to the first two and
`non_gki` to the third); both Flux writers share one file in two formats; "legacy" is defined as
"not GKI and old", i.e. a single axis. The directive requires two independent axes
(Integration: GKI / NonGKI / Unknown; Generation: Legacy / NonLegacy / Unknown) with confidence.
No capability probing exists beyond per-write "node exists" checks.

## 6. Device / SoC classification today
`customize.sh` sets an integer `SOC` (1 MediaTek, 2 Snapdragon, 3 Exynos, 4 Unisoc, 5 Tensor,
6 Tegra, 7 Kirin) from sysfs markers, then substring matches over device-tree model, several
`getprop` values and `/proc/cpuinfo`. Patterns such as `*mt*`, `*sm*`, `*sp*`, `*gs*`, `*SC*` can
match unrelated strings. `flux_utility.sh logcat` decodes the same file with a different table
(6 = Intel, 7 = Tegra, 8 = Kirin). `DeviceInfo` and `PlatformProbe` exist in C++ but do not drive
tweak eligibility. There is no capability-tier model; Snapdragon devices share one branch.

## 7. HiCo Thermal (future Synrei) today
- `hicod` (C++20, ndk-build + host CMake). Follows Flux: reads `current_profile`, `gameinfo`,
  `.lock`, `synthesis_core.json`, and gates on Flux `versionCode ≥ 46`.
- Levels: **Relaxed** (vendor thermal daemons keep running with chipset-tuned configs,
  bind-mounted and verified), **Max** (thermal daemons stopped, zones → `user_space`, CPU/GPU
  cooling devices released, cpufreq caps lifted; games only), mode **Extreme** (also raises passive
  trips on zones without `user_space`, may stop the thermal HAL). Optional `thermal_overclock`
  enables cpufreq boost frequencies.
- Safety: `SafetyGuard` (CPU 95 °C / battery 46 °C, hysteresis, 30 s cooldown) and
  `HeadroomGuard` (max only with margin); kernel critical trips are never touched per code
  comments; journal on tmpfs written **before** each change; `hicod restore` on boot, uninstall and
  Flux uninstall.
- Integrity: Ed25519-signed releases, SHA-256; device database compiled into the binary
  (`DeviceDatabase.gen.cpp`, CI `--check`).
- Data: ~211 MB of thermal source data, evidence, candidates and generated databases (see HiCo
  inventory). `README.md` on `main` currently contains "Phase 2 Final Local Cleanup" instructions
  rather than a project README (BLOCKERS B-06).
- Relationship to directive §59: Max/Extreme remove vendor thermal throttling under HiCo's own
  software ceiling. Synrei's "final thermal authority" and "no mode may disable fundamental thermal
  safety" must be evaluated against these levels in Phase 9 (BLOCKERS B-04). No change now.

## 8. Ownership boundaries today

| Concern | Owner today | Contract |
|---|---|---|
| Game detection, focus, PID tracking | fluxd | `gameinfo`, `current_profile` |
| System state collection | NativeMonitor / SynthesisCore | `synthesis_core.json` v3 |
| Performance tweaks (CPU/GPU/sched/VM/I/O/net/touch/refresh/zram) | flux_profiler.sh | invoked by fluxd |
| Thermal zones / daemons / cooling devices | HiCo when active, else Flux sets `step_wise` once | `hico_active()` check |
| Thermal tiering of games (Performance ↔ Lite) | fluxd | headroom / status / CPU °C |
| Session statistics + refresh matching | SessionRecorder (observer **and** policy) | `session_live.json`, `sessions.json` |
| Capability facts | `fluxd capabilities` (graphics) | `capabilities.json` v4 |

## 9. Validation baseline (this audit)
See `docs/agent-state/VALIDATION.md`. Summary: Flux host tests 7/7 PASS, Flux WebUI build PASS,
HiCo ctest 2/2 PASS, HiCo Python suites 15/15 PASS, `gen_device_db --check` up to date. Android
builds, SynthesisCore Gradle tests and all device behaviour: NOT_TESTED (no NDK / Android SDK /
device in this environment).
