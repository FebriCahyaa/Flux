# Brand & Namespace Migration (Phase 4.5)

**Status:** IMPLEMENTED on the host.
**Android CI:** not checked.
**Device:** NOT_TESTED.

This phase changes the public identity only. Runtime behavior is unchanged, apart from two
user-visible strings:
- the human-readable notification title;
- the module description string.

The following are unchanged:
- policy, decision, transactions, thermal and telemetry;
- every event and schema.

## Identity

| Name | Role |
|---|---|
| **Zairenkai** | Ecosystem / platform. Public name of the module, daemon and CLI. |
| **Synrei** (Synrei Thermal Intelligence) | Thermal intelligence. |
| **Zairenkai Intelligence** | Public intelligence / core layer. |
| **Aeyrin** | Internal codename only. Never a public product or platform name. |
| **Flux** (Flux Tweaks) | Legacy / compatibility runtime identity: binary, module ID, paths, namespaces. |
| **HiCo** | Legacy / compatibility thermal backend identity: `/dev/hico/state`, `hicod`, paths. |
| **SynthesisCore** | Legacy / compatibility package / component identity (`com.febricahyaa.synthesiscore`). |

**A legacy identifier does not imply that the underlying implementation is obsolete.** Flux, HiCo
and SynthesisCore are the live implementations behind the Zairenkai names.

No legal or trademark claim is made by this document.

## Migration map

The table below is the forensic inventory. File counts come from `git grep` on
`integration/game-runtime-clean` before this phase.

| Old identifier | Current consumers | Kind | Safe to migrate? | Required alias | Risk if changed |
|---|---|---|---|---|---|
| `Flux Tweaks` (label) | `module.prop` name, CLI banner/version, notification title, daemon start/exit log line, `customize.sh`/`action.sh` messages, README, `jni/README.md`, WebUI/website `<title>`, `NOTICE.md`, CI titles, `changelog.md`, `telegram.py` | public | Yes, human-readable text only. Done where listed under "Changed", except `NOTICE.md` (legal), CI (unrelated) and changelog (history). | Shown as "(Flux Tweaks runtime)" | Low |
| `flux` module ID (`id=flux`) | `module.prop`; `/data/adb/modules/flux` in `Flux.hpp` `MODPATH`, `customize.sh` `BIN_PATH`, scripts, WebUI | compatibility | **No**: root managers key installs and updates by ID | n/a (frozen) | Breaks upgrades and creates a duplicate module |
| `fluxd` | `jni/Android.mk` `LOCAL_MODULE`, `service.sh` (`fluxd daemon`), `customize.sh` (setup, symlinks), `uninstall.sh`, `flux_profiler.sh`, 12 WebUI files (exec calls), 22 docs, 8 tests | compatibility | **No** | Public alias `zairenkai` → `fluxd` (symlink) | Breaks boot service, WebUI, scripts and user muscle memory |
| `flux_profiler`, `flux_utility` | `customize.sh`, `uninstall.sh`, `fluxd` (`Profiler.cpp`), WebUI | compatibility | No | none | Breaks profile application and log export |
| `/data/adb/.config/flux` | `Flux.hpp` `CONFIG_DIR` (lock, log, config, gamelist), `RefreshHold.cpp`, `service.sh`, `customize.sh`, `uninstall.sh`, `flux_profiler.sh`, `flux_utility.sh`, 7 WebUI files, README, 3 docs, `synthesis_core_test` | compatibility (user data) | **No automatic move** | Compatibility lookup (`jni/brand/ConfigNamespace`) | User configuration loss |
| `/data/adb/.config/zairenkai` | telemetry, `installation.json`, `verification.journal`, `policy.journal` (Steps 8.11–4C) | already new namespace | n/a | n/a | Unchanged; the epoch is never reset |
| `flux_capability`, `flux_session`, `flux_runtime`, `flux::*`, `Flux*` NDK modules, `flux_*` CMake targets, `FluxCLI.*`, `FluxProfileMode` | `jni/`, tests, `CMakeLists.txt`, docs; `FluxProfileMode` also in `PolicyEvidence.hpp` and WebUI stores (numeric profile values) | technical identifier | Option only (source-level rename, ABI-neutral) | n/a | Large diff with no user benefit; deferred |
| `FluxTweaks` (log tag) | `Flux.hpp` `LOG_TAG`; logcat filters, `flux_utility.sh` log export | technical identifier | No | n/a | Breaks log collection |
| `HiCo`, `hico` | `uninstall.sh` (`hicod restore`), `flux_profiler.sh`, `flux_utility.sh`, `Main.cpp`, `SessionHost.cpp`, `jni/thermal`, website, README, changelog | compatibility (thermal backend) | No (separate repository and ownership) | Public label "Synrei Thermal Intelligence" | Breaks thermal restore on uninstall |
| `/dev/hico/state` | `SynreiThermalAdapter` (`kSynreiStatePath`), `SessionHost.cpp`, 3 tests, docs | compatibility (kernel/IPC-facing) | **No** | n/a | Synrei evidence lost; policy blocks |
| `/data/adb/.config/hico` | `flux_profiler.sh`, `flux_utility.sh` | compatibility | No | n/a | HiCo state and report break |
| `SynthesisCore` | `jni/base` (`SynthesisCore` NDK module), `jni/gfx`, 12 WebUI files, `service.sh`, `prebuilt/`, sync workflow/scripts, website, docs | compatibility (component) | Public label only | "Zairenkai Intelligence" | Breaks build module names and sync CI |
| `com.febricahyaa.synthesiscore` | `service.sh` (`app_process` main class), `flux_utility.sh`; APK manifest in `prebuilt/` | compatibility (package ID) | **No** | n/a | APK signature/pin and IPC break |
| `prebuilt/synthesiscore.*` | `customize.sh`/`service.sh` install and pin, sync workflow | compatibility (tracked data) | No | n/a | Supply-chain pin mismatch |
| Update channel (`updateJson=.../FebriCahyaa/Flux/main/update.json`, `update*.json`, `update/changelog.md`) | `module.prop`, `compile_zip.sh` (per-flavor rewrite), release workflow, root managers | compatibility | **No** | n/a | Existing installs stop receiving updates |
| `github.com/FebriCahyaa/{Flux,HiCo,SynthesisCore}` | README, `jni/README.md`, `prebuilt/README.md`, update JSONs, website, `About.vue`, 2 docs | compatibility (external) | No (repository rename is an infrastructure decision) | n/a | Broken links and update URLs |
| Event schema v1, 19 Observatory type names, sources/categories | `jni/observatory`, bridge, tests | technical identifier | **No** | n/a | Telemetry history becomes unreadable |
| Assets (`banner.webp`, `art/`, `jni/diagram.svg`, WebUI `flux_*.avif`) | README, `module.prop` banner, WebUI | intentional legacy | Not replaced (data lock) | n/a | n/a |
| `Aeyrin` | `jni/kernel` comment, 2 docs | internal codename | stays internal | n/a | n/a |

## Changed (public, human-readable only)

| File | Change |
|---|---|
| `module/module.prop` | `name=Zairenkai`; description names Synrei Thermal Intelligence and Zairenkai Intelligence. `id`, `updateJson`, `banner` and `webuiIcon` are unchanged. |
| `jni/FluxCLI.cpp` | Banner "Zairenkai CLI (Flux Tweaks runtime)", version line, daemon command description, usage line showing the invoked name. Same command table and handlers. |
| `jni/include/Flux.hpp` | `NOTIFY_TITLE` "Zairenkai". `LOG_TAG`, `CONFIG_DIR` and `MODPATH` are unchanged. |
| `jni/Main.cpp` | Start/exit log text and the module description status text. No logic change. |
| `module/customize.sh` | Install messages. Adds the `zairenkai` → `fluxd` symlink on both mount paths. |
| `module/uninstall.sh` | Also removes the `zairenkai` symlink that `customize.sh` creates. Only symlinks are removed. |
| `module/action.sh` | Message. |
| `webui/index.html`, `website/index.html` | `<title>`. |
| `README.md`, `jni/README.md` | Title and an identity / compatibility section. |
| `jni/brand/*` | Brand labels, frozen identifier constants, configuration compatibility helper. |

## Compatibility strategy

Public branding and technical namespace migration are separate operations. Every identifier in the
"No" rows of the migration map is frozen. Those identifiers are listed in
`jni/brand/Brand.hpp`, and `brand_migration_test` pins them.

The following change only through an explicit migration mechanism, never for branding:
- package IDs, module IDs, persistent storage locations and configuration paths;
- event schema IDs, update channels and ABI identifiers;
- CLI commands, kernel-facing paths and the HiCo state path.

## Public entrypoint and CLI

- **Entrypoint chain:** `zairenkai` → symlink → the one `fluxd` binary.
- **Install:**
  - Magisk mounts `$MODPATH/system/bin/zairenkai`.
  - KernelSU/APatch get `/data/adb/{ksu,ap}/bin/zairenkai`.
- **Single implementation:** no second daemon, launcher or command handler. `flux_cli()` stays the only dispatcher. `zairenkai telemetry status` and `fluxd telemetry status` run the same handler.
- **Help output:** shows the invoked name.
- **Daemon start:** `service.sh` still starts `fluxd daemon`.

## Configuration namespace

`/data/adb/.config/flux` is **not moved** and stays the runtime's configuration directory.
`jni/brand/ConfigNamespace` defines the migration path and has these properties:
- **Compatibility lookup:** `resolve()` returns `/data/adb/.config/zairenkai/config/<name>` when that file exists, otherwise the legacy file.
- **Planned migration:** `plan()` + `apply()` copy legacy → new only when the new file is absent.
- **Create-only writes:** never delete, move, truncate or overwrite. The legacy file always stays.
- **Rerun-safe:** deterministic and idempotent; a rerun copies nothing.
- **Allowed names:** plain file names only (no `/`, no `..`).

**fluxd does not call the helper in this phase.** Wiring it in would let a file under the new
directory override user configuration, which is a behavior change. Enabling it is a future,
separately reviewed step. Telemetry, the installation epoch and journals already live under
`/data/adb/.config/zairenkai` and are untouched.

## Synrei / HiCo boundary

- **Public name:** Synrei Thermal Intelligence.
- **Backend:** HiCo (`hicod`, `/dev/hico/state`, `/data/adb/.config/hico`), unchanged.
- **No behavior changes:**
  - thermal behavior, control ownership and polling are unchanged;
  - no new thermal events.
- **HiCo repository:** not modified.

## SynthesisCore / Aeyrin

- **Public name:** Zairenkai Intelligence.
- **Aeyrin:** the internal codename only.
- **Package ID:** `com.febricahyaa.synthesiscore` stays a compatibility identifier, with no package rename.
- **Unchanged:**
  - the `SynthesisCore` NDK module;
  - the APK, certificate pin and sync workflow;
  - the SynthesisCore repository.
- **Source-level renames:** a source namespace layer (for example `zairenkai::`) is possible as an option only. It would be ABI-neutral for the static modules, but it is deferred because it has no user benefit and a large diff.

## Repository names and releases

- **GitHub repositories:** not renamed and remotes not changed. Renaming them is a future release / infrastructure decision; the update URL and the APK source would need redirects first.
- **Versioning:** releases can be presented as **ZAIRENKAI** with the component labels above. Semantic versioning continues and nothing is reset.
- **Not rebranded:**
  - CI workflow names, which are unrelated CI;
  - `NOTICE.md`, which is legal attribution;
  - `changelog.md`, which is history.

## Remaining old identifiers

Every tracked file that still contains `flux`, `hico` or `synthesiscore` in any letter case is
classified in `docs/architecture/brand_identifiers.tsv` as one of:
- compatibility;
- technical identifier;
- historical documentation;
- migration documentation;
- test fixture;
- intentional legacy reference.

`tests/brand_inventory_test.sh` fails on any unclassified file. Nothing was deleted to reach that state.

## Tests

`brand_migration_test` and `brand_inventory_test` cover:
- CLI names and labels;
- frozen identifiers (config dir, module ID, update channel, HiCo path, package ID, telemetry root, schema v1, 19 event names, installation epoch path);
- configuration lookup and migration: legacy readable, no overwrite, no loss, deterministic, idempotent, rerun-safe, race-safe;
- a single daemon executable and a single CLI dispatcher;
- no thread, transaction, journal or thermal code in the brand layer;
- DecisionEngine, PolicyExecutor, LivePolicyController and Transaction sources untouched.
