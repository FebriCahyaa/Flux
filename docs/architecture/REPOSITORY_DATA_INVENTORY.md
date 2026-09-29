# Repository Data Inventory — Flux (future Zairenkai)

Phase 0 deliverable. Required by the Repository Data Preservation Lock: no file or directory in
this repository may be moved, renamed, regenerated-over or deleted until it is listed here with its
role, consumers and classification. Companion inventories live in the other two repositories:

- `FebriCahyaa/HiCo` → `docs/architecture/REPOSITORY_DATA_INVENTORY.md`
- `FebriCahyaa/SynthesisCore` → `docs/architecture/REPOSITORY_DATA_INVENTORY.md`

| | |
|---|---|
| Audited commit | `main` @ `b75491c73121cc43025ca4df7c67a297db449423` |
| Audit branch | `ccr-0dc934d1-0a6zta` (branched from that commit, no code changes) |
| Tracked files | 299 (`git ls-files`), plus 2 uninitialised submodules |
| Audit date | 2026-09-29 |

## Classification key

| Class | Meaning |
|---|---|
| AUTHORITATIVE | The source of truth. Nothing else can recreate it exactly. |
| DEPENDENCY | Required by build, packaging, CI or runtime; content owned elsewhere or pinned. |
| HISTORICAL | Record of past behaviour/releases. Value lies in being unchanged. |
| TEST | Test code or fixture. |
| FIXTURE | Data consumed only by tests. |
| GENERATED | Produced by a tool, but tracked in git and consumed as-is. |
| DERIVED | Reproducible from other tracked inputs; still tracked. |
| UNKNOWN | Role not proven. **Preserved.** Never read as "obsolete". |

"Reproducible: yes" never means "disposable" (see `DELETION_MANIFEST.md`).

## Root files

| Path | Role | Consumers / references | Class | Reproducible | CI | Runtime | Tests |
|---|---|---|---|---|---|---|---|
| `version` | Module version string (`1.4.1`) | `.github/scripts/compile_zip.sh`, `release.yml` | AUTHORITATIVE | no | yes | via module.prop | no |
| `update.json` | Root-manager update channel (universal zip) | `module/module.prop` `updateJson` URL on **main** of every installed device; rewritten by `release.yml` | AUTHORITATIVE | by release only | yes | **yes — remote, installed devices poll it** | no |
| `update-arm64.json` | Update channel, arm64 zip | `compile_zip.sh` rewrites module.prop per flavor; installed arm64 devices | AUTHORITATIVE | by release only | yes | yes (remote) | no |
| `update-arm.json` | Update channel, arm zip | as above, arm devices | AUTHORITATIVE | by release only | yes | yes (remote) | no |
| `gamelist.txt` | Default list of 500+ game packages | `compile_zip.sh` → zip → `customize.sh` `fluxd setup_gamelist` → `/data/adb/.config/flux/gamelist.json` | AUTHORITATIVE | no | packaged | install-time | no |
| `changelog.md` | Human changelog | README, release notes | HISTORICAL | no | no | no | no |
| `banner.webp` | Module banner image | `module.prop` `banner=`, `compile_zip.sh`, README | AUTHORITATIVE (asset) | via `art/` | packaged | root-manager UI | no |
| `README.md` | Project documentation | GitHub, website | AUTHORITATIVE | no | no | no | no |
| `LICENSE` | Apache-2.0 | `compile_zip.sh` copies into zip | AUTHORITATIVE (legal) | no | packaged | shipped | no |
| `NOTICE.md` | Third-party notices (Encore Tweaks, KTweak, libraries) | `compile_zip.sh` copies into zip; Apache-2.0 §4 obligation | AUTHORITATIVE (legal) | no | packaged | shipped | no |
| `CMakeLists.txt` | Host test harness (not the device build) | `build.yml` `host_tests` job | AUTHORITATIVE | no | yes | no | yes |
| `.gitmodules` | Submodules `jni/external/spdlog`, `jni/external/rapidjson` | checkout `submodules: true`, CMake, Android.mk | DEPENDENCY | no | yes | no | yes |
| `.gitattributes`, `.gitignore` | Repository metadata | git | DEPENDENCY | no | yes | no | no |

## Directories

| Path | Files | Role | Consumers / references | Class | Reproducible | CI | Runtime | Tests |
|---|---|---|---|---|---|---|---|---|
| `jni/*.cpp,*.hpp` | 17 | `fluxd` daemon: main loop, CLI, config store, profiler bridge, session recorder, refresh hold, render booster, device mitigation, inotify | `jni/Android.mk` (wildcard `*.cpp`), `CMakeLists.txt` (RefreshHold, RenderBooster) | AUTHORITATIVE | no | yes | yes (binary) | yes |
| `jni/base/*` | 29 | Static libs: BinderNDK, DeviceInfo, FluxUtility, GameRegistry, InotifyWatcher, LockFile, NativeMonitor, PIDTracker, SynthesisCore reader | `jni/base/Android.mk`, `LOCAL_STATIC_LIBRARIES` | AUTHORITATIVE | no | yes | yes | partial (SynthesisCore reader) |
| `jni/gfx/*` | 11 | Capability model v4, Vulkan/platform probes, `fluxd capabilities` | `jni/gfx/Android.mk` (`FluxGfx`), CMake `flux_gfx` | AUTHORITATIVE | no | yes | yes | yes |
| `jni/gfx/capability_schema_v4.json` | 1 | Capability schema descriptor; header says the schema is **owned by SynthesisCore** (see BLOCKERS B-03) | `capability_schema_test` via `FLUX_SCHEMA_JSON` define | AUTHORITATIVE (mirror) + FIXTURE | no | yes | no | yes |
| `jni/include/*` | 7 | Shared headers: paths (`Flux.hpp`), logging, exec, signals, module.prop | every daemon source | AUTHORITATIVE | no | yes | yes | yes |
| `jni/external/Android.mk` | 1 | ndk-build glue for submodules | `jni/Android.mk` | DEPENDENCY | no | yes | no | no |
| `jni/external/rapidjson`, `jni/external/spdlog` | submodules | JSON parser, logger | Android.mk, CMake (rapidjson) | DEPENDENCY | from upstream at pinned SHA | yes | linked | yes |
| `jni/README.md`, `jni/diagram.svg` | 2 | Daemon documentation and workflow diagram | README links | HISTORICAL / docs | no | no | no | no |
| `jni/.clang-format` | 1 | Formatting rules | developers | DEPENDENCY | no | no | no | no |
| `scripts/flux_profiler.sh` | 1 (2,339 lines) | Executes every profile: CPU/GPU/devfreq, uclamp, VM, I/O, touch, net, refresh, zram, props; kernel-type detection | `compile_zip.sh` → `system/bin/flux_profiler`; `jni/Profiler.cpp` | AUTHORITATIVE | no | packaged | **yes** | indirect |
| `scripts/flux_utility.sh` | 1 | Utility entry (`capabilities`, `change_cpu_gov`, `change_gpu_gov`, `save_logs`, `logcat`, report); dispatches `$@` | `compile_zip.sh` → `system/bin/flux_utility`; WebUI `exec` | AUTHORITATIVE | no | packaged | **yes** | no |
| `module/*.sh`, `module.prop`, `META-INF/` | 9 | Installer, boot service, verification, uninstall, action | root managers; `compile_zip.sh` | AUTHORITATIVE | no | packaged | **yes** | no |
| `module/{config,webroot,system/bin}/.placeholder` | 3 | Keep directories that packaging fills | `compile_zip.sh` (excluded from zip by `*placeholder*`) | DEPENDENCY | trivially | yes | no | no |
| `config/device_mitigation.json` | 1 | Per-SoC/device rules that skip misbehaving tweaks | `compile_zip.sh` → `/data/adb/.config/flux/device_mitigation.json`; `DeviceMitigationStore`; WebUI `DeviceMitigation.vue` | AUTHORITATIVE | no | packaged | **yes (fatal if unparsable)** | no |
| `prebuilt/synthesiscore.apk` | 1 | Pinned SynthesisCore v2.1.1 release APK (runs as root) | `compile_zip.sh` (checksum gate), `service.sh` (per-boot re-check), `sync_synthesiscore.yml` | DEPENDENCY (GENERATED upstream, tracked) | only by the verified sync workflow | yes | **yes** | no |
| `prebuilt/synthesiscore.apk.sha256`, `.cert.sha256`, `synthesiscore.json` | 3 | Checksum pin, signing-certificate pin, release metadata | `compile_zip.sh`, `customize.sh`/`verify.sh`, sync workflow | AUTHORITATIVE (security pins) | **no — must never be hand-edited** | yes | yes | no |
| `prebuilt/README.md` | 1 | Supply-chain documentation | developers | AUTHORITATIVE | no | no | no | no |
| `update/changelog.md` | 1 | Changelog served to root managers | `update*.json` `changelog` URL; `release.yml` | AUTHORITATIVE (remote channel) | by release only | yes | remote | no |
| `tests/*` | 8 | Host tests (C++20): refresh hold, render booster, capability model/schema, Vulkan/platform probes, SynthesisCore reader | `CMakeLists.txt`, `build.yml` | TEST | no | yes | no | yes |
| `webui/` (src, public, config) | 155 | Vue 3 + Vite WebUI (Material 3 Expressive, 10 locales) | `build-module` action (`bun run build` → `module/webroot`) | AUTHORITATIVE | no | yes | shipped | build only |
| `webui/bun.lock` | 1 | Dependency lock (`--frozen-lockfile`) | CI | DEPENDENCY | no (pins versions) | yes | no | no |
| `website/` | 8 | Public website (+ `modules.json`) | `deploy_website.yml` | AUTHORITATIVE | no | yes | no | no |
| `art/` | 5 | Banner generator sources (HTML/JS/Python) | produces `banner.webp` | AUTHORITATIVE (asset source) | no | no | no | no |
| `.github/workflows/*` | 5 | build, release, sync_synthesiscore, deploy_website, ksurepo_sync | GitHub Actions | AUTHORITATIVE (CI) | no | — | no | — |
| `.github/actions/build-module` | 1 | Shared composite build (NDK r29, Bun, zip) | build.yml, release.yml | AUTHORITATIVE (CI) | no | — | no | — |
| `.github/scripts/*` | 7 | compile_zip, gen_sha256sum, changelog, sync, Telegram notify | workflows | AUTHORITATIVE (CI) | no | — | no | — |
| `.github/ISSUE_TEMPLATE/*` | 3 | Issue forms | GitHub | AUTHORITATIVE | no | no | no | no |

## Runtime data written on the device (not repository data)

Listed so later phases do not confuse repository retention with runtime retention. None of these
paths is in git. The planned 7-day telemetry retention (Phase 5) applies **only** to the future
`/data/adb/.config/zairenkai/telemetry/` tree, never to anything in the tables above.

All paths below are under `/data/adb/.config/flux/` unless absolute.

| Path | Producer | Consumers | Contract notes |
|---|---|---|---|
| `config.json` | `FluxConfigStore` (default created), WebUI | fluxd, WebUI | User settings. Must survive updates. |
| `gamelist.json` | `fluxd setup_gamelist` at install, WebUI | fluxd `GameRegistry`, WebUI, **HiCo WebUI** | Missing/unparsable = fatal daemon start error. |
| `device_mitigation.json` | installer (from `config/`) | fluxd, WebUI | Unparsable = fatal daemon start error. |
| `current_profile` | fluxd | flux_utility, **HiCo `FluxLink`** | **Frozen external contract** (integer `FluxProfileMode`). |
| `gameinfo` | fluxd | **HiCo `FluxLink`** | **Frozen external contract** (`<package> <pid> <uid>` or `NULL 0 0`). |
| `.lock` | fluxd singleton lock | fluxd, **HiCo** (`daemon_running()` via F_GETLK) | External contract. |
| `synthesis_core.json` | NativeMonitor or SynthesisCore Java daemon | fluxd (inotify), flux_utility, **HiCo** (`FLUX_STATUS_FILE`) | Plain-text key/value despite `.json` name; protocol v3. |
| `binder_codes` | `synthesiscore.apk --resolve` at boot | NativeMonitor | Rebuilt every boot. |
| `monitor_mode` | fluxd | service.sh, flux_utility | `native` / `java`. |
| `java.lock`, `sysmon.pid`, `sysmon_watchdog.pid` | service.sh / SynthesisCore | fluxd, service.sh | Rebuilt every boot. |
| `capabilities.json` | `fluxd capabilities` | WebUI capability store | Schema v4. |
| `session_live.json` | `SessionRecorder` (1 Hz) | WebUI Monitor | Live session. |
| `sessions.json` | `SessionRecorder` | WebUI Monitor / SessionDetail | **Count-based retention: last 30 sessions** (not time-based). |
| `flux.log`, `flux.prev.log` | fluxd (2 MB rotation), service.sh | WebUI log view, `save_logs` | |
| `sysmon.log`, `sysmon.log.prev` | service.sh, SynthesisCore | `save_logs` | |
| `soc_recognition` | `customize.sh` | flux_profiler, flux_utility | Integer SoC code; see BLOCKERS B-07 (inconsistent decoding). |
| `is_gki`, `kernel_type` | `service.sh` **and** `flux_profiler.sh` (last writer wins) | flux_utility, WebUI | Two different formats; see BLOCKERS B-05. |
| `thermal_api_status` | service.sh | WebUI | |
| `default_cpu_gov` | service.sh | FluxConfigStore, profiler | |
| `props_orig`, `props_stage`, `refresh_adaptive_orig`, `refresh_adaptive_status`, `zram_orig`, `zram_applied` | flux_profiler | flux_profiler (restore), flux_utility | Partial rollback state for system tweaks. |
| `/dev/.flux_refresh_orig` (tmpfs) | flux_profiler (`flux_refresh`) | `SessionRecorder` `RefreshMatcher`, profiler restore | User's refresh setting held while a game runs; gone after reboot. |
| `/data/adb/modules/flux/module.prop` (+ `.orig`) | installer, fluxd (status text) | root managers, **HiCo** (version gate `FLUX_MIN_VERSION_CODE 46`) | Module id `flux` is an external contract. |
| `/data/adb/service.d/.flux_cleanup.sh` | service.sh | root manager boot | Cleanup after uninstall. |
| `/sdcard/Download/<bugreport>.tar.gz` | `flux_utility save_logs` | user | Includes HiCo log/state when present. |

## Unmerged work that is not in this inventory's commit

Branch `origin/ccr-23925ebb-375wkg` (`f811dad`, main + 11 commits, 0 behind) adds 74 files
(`jni/compat/`, `jni/zygisk/`, `tools/compat-testapp/`, `docs/GAME_RUNTIME.md`, validation scripts,
tests) and runtime paths `compat_library.json`, `compat_games.json`, `game_profiles.json`,
`compat_journal`, `compat_status.json`, `compat_zygisk_optin`, `fluxd.pid`, `compat_provider/`.
That branch must be inventoried again when it is merged. See `ZAIRENKAI_BASELINE.md` §3.

Branch `origin/sync/synthesiscore-v2.1.0` (`676b80c`, 1 ahead / 89 behind) is a superseded sync
PR branch (main already pins v2.1.1). It is preserved; no branch is deleted by this programme.

## UNKNOWN entries

None in this repository at the audited commit. Every tracked path has an identified consumer.
