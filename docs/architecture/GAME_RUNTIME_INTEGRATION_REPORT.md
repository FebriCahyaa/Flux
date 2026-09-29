# Game Runtime Integration Report (Phase 1.5 — audit only)

**Status: AUDIT COMPLETE. Nothing merged, cherry-picked, renamed or migrated.**
Integration is **not** complete; this report is the input for the owner's merge decision (D-09).

| | |
|---|---|
| Branch audited | `origin/ccr-23925ebb-375wkg` @ `f811dad8331470923b3eda0edb71ede39b6ab544` |
| Compared against | `origin/main` @ `b75491c` (merge base = main; branch is 11 ahead, 0 behind) |
| Governing decisions | D-04 frozen contracts, D-09 no direct merge, D-10 Zygisk constraints, D-11 schema ownership |
| Date | 2026-09-29 |

## 1. Branch difference

### Commits (oldest first)
| Commit | Subject |
|---|---|
| `497a08e` | feat(compat): add Flux Compatibility Engine core |
| `786ef1e` | feat(webui): add Game Runtime section and compat_analyze diagnostics |
| `1f7e89f` | feat(runtime): connect GameRuntime to daemon lifecycle |
| `0e8d70b` | feat(refresh): connect game runtime to the game-time refresh writer |
| `27b0e76` | test(runtime): add real-device validation script for the Game Runtime |
| `0190333` | feat(zygisk): add provider plan contract, GOT patcher and in-process wrappers |
| `aa8b678` | feat(zygisk): add companion, arming and process-verified backend |
| `7922329` | feat(zygisk): arm plans from the daemon, provider UI, test app and docs |
| `c540fa5` | fix(bugreport): include Game Runtime and provider state in save_logs |
| `9e1c75e` | fix(zygisk): ship the provider switched off and stay out of non-app processes |
| `f811dad` | fix(webui): say when the provider will load instead of blaming Zygisk |

### Files: 74 changed, +11,002 / −21 — 58 added, 16 modified, **0 removed**

Added (58): `jni/compat/*` (21: CompatTypes, Resolver, GameProfile, PerfPlanner, Runtime,
GameRuntime, Session, Hardware, ProviderPlan, ProviderPaths, Arming, ZygiskBackend + .cpp),
`jni/zygisk/*` (8: Android.mk, FluxCompatModule.cpp, GotHook, Interpose, `include/zygisk.hpp`),
`scripts/flux_runtime_validate.sh`, `scripts/flux_provider_validate.sh`, `tests/*` (10),
`tools/compat-testapp/*` (9), `webui/src/components/GameRuntimeSection.vue`,
`webui/src/stores/GameRuntime.js`, `docs/GAME_RUNTIME.md`.

Modified (16): `.github/scripts/compile_zip.sh`, `CMakeLists.txt`, `NOTICE.md`, `changelog.md`,
`jni/Android.mk`, `jni/FluxCLI.cpp` (+`compat_analyze`, `compat_arm`), `jni/Main.cpp` (+62/−12:
SessionRuntime begin/after_profile/end hooks, journal recovery), `jni/Profiler.cpp` (refresh
request env), `jni/include/Flux.hpp` (+7 path defines), `module/customize.sh` (ships
`zygisk_provider/` switched off), `module/service.sh` (capability probe at boot),
`scripts/flux_profiler.sh` (`flux_refresh` single writer + per-game target Hz),
`scripts/flux_utility.sh` (+59: provider enable/disable, reports), `webui/.../GameSettings.vue`
(arms plan before launch), `en.json`/`id.json` (+125 each).

### Modified architecture
- New runtime layer between `Main.cpp` game lifecycle and `flux_profiler.sh`: `SessionRuntime`
  (idempotent begin/end, persisted journal, crash recovery) → `GameRuntime` (compat then perf) →
  `Transaction`/`Action` (snapshot → apply → verify → restore → verify_restore).
- New device-side files (all under `/data/adb/.config/flux/`): `compat_library.json`,
  `compat_games.json`, `game_profiles.json`, `compat_journal`, `compat_status.json`,
  `compat_zygisk_optin`, `fluxd.pid`, `compat_provider/{plans,proc,provider.json}`;
  module dir: `armed.list`, `no_provider`, `zygisk_provider/`, `zygisk/`.
- Frozen contracts (D-04) `current_profile`, `gameinfo`, `.lock`, `synthesis_core.json`,
  `FluxProfileMode`, module id, update channels: **unchanged** by this branch.

## 2. Game Runtime ownership analysis

| Area | Implementation | Verdict | Reason |
|---|---|---|---|
| GameRuntime lifecycle | `GameRuntime` activate_compat → activate_perf → reassert_perf → deactivate | **MODIFY** | Sound lifecycle; must be split so the perf path never depends on the identity path (today `activate()` runs compat first). |
| Session management | `SessionRuntime` begin/end idempotent, one session at a time, EndReason, hooks into existing PID/focus logic | **KEEP** | Reuses Main.cpp detection, adds nothing that detects games; matches Zairenkai Runtime ownership. |
| Profile inheritance | `ProfileLibrary::resolve` GLOBAL → preset(`extends`, cycle-checked) → GAME → RUNTIME | **MODIFY** | Matches directive §17 hierarchy. Remove `CompatSettings` identity fields (device/cpu/gpu_profile, identities library). |
| Launch boost | `LaunchBoost` bounded (8 s), cancelable, restorable via Actions | **KEEP** | Deadline, cancel reasons, transactional restore. |
| Memory planner | `PerfPlanner` memory/touch/storage levels; node-exists gating; mitigation respected; zram untouched | **KEEP** | Needs later migration to tweak contracts (Phase 3) and capability eligibility. |
| Refresh strategy | Per-game `refresh` → `FLUX_REFRESH_TARGET_HZ`; `flux_refresh` single writer; unsupported rate ignored, never forced | **KEEP** (MODIFY in Phase 7) | Legitimate; must later coexist with `RefreshMatcher` split (B-12). |
| Compatibility engine | Resolver: requirement − real hardware = gap → identity layers to override | **REDESIGN (compat part) / KEEP (analysis part)** | See §3. Gap-closing by identity substitution is forbidden (D-10). |
| Transaction system | `Action`/`NodeWrite`/`Transaction`, snapshot-before-apply, verify, verify_restore | **KEEP** | Best existing implementation of directive §15/§58; candidate for Phase 11 executor. |
| Rollback | Roll back applied actions on any failure | **KEEP** | |
| Restore | Journal `compat_journal` (atomic write), `recover()` before any device access, deleted only when all verified | **KEEP** | Crash-safe; aligns with HiCo journal model. |

## 3. Compatibility Engine review

| Capability | Where | Allowed? | Verdict |
|---|---|---|---|
| Capability analysis (`RealHardware` from capability model; spoofed identity never used as evidence) | `Hardware.*` | Allowed | **KEEP** |
| Requirement matching (`KnownGameDb`, `GateKind`, `Resolution` with separate `unlocked` / `capable` / `sustained`) | `Resolver.*` | Allowed | **KEEP** (analysis + diagnostics only) |
| Supported feature exposure: `Display` layer (refresh rate the panel really offers) | `Resolver`, `flux_refresh` | Allowed | **KEEP** |
| Honest unknown-game answer ("no reliable profile", never a guessed spoof) | `Resolver` | Allowed | **KEEP** |
| Device identity layer (`ro.*` props, `android.os.Build` fields via JNI `SetStaticObjectField`) | `ProviderPlan`, `FluxCompatModule`, `Interpose` (`__system_property_get`) | **Forbidden — fake device identity** | **REMOVE OR REDESIGN** |
| CPU identity layer (Build/props CPU fields) | same | **Forbidden — fake CPU identity** | **REMOVE OR REDESIGN** |
| GPU identity layer (`glGetString` VENDOR/RENDERER/VERSION, `eglQueryString`, Vulkan `deviceName`/`vendorID`/`deviceID`/`driverVersion`) | `Interpose`, `GotHook` | **Forbidden — fake GPU identity** | **REMOVE OR REDESIGN** |
| Vulkan `apiVersion` rewrite (clamped, not raised above real) | `Interpose` (`vk_api_clamped`) | Borderline — lowers only, but still rewrites a reported property | **REDESIGN** (drop unless proven needed for a real driver bug, then document) |
| Identity library / `compat_library.json` `identities`, modes Compatibility/Advanced/Custom | `GameProfile`, `GameRuntime` | Forbidden in current form | **REMOVE OR REDESIGN** |
| `GateKind::DeviceIdentity/CpuIdentity/GpuIdentity` as *diagnosis* | `CompatTypes` | Allowed as a diagnosis ("game gates on identity; not satisfiable without spoofing") | **KEEP as diagnosis only** |
| Anti-cheat bypass | none found | — | None present |
| Anti-detection / stealth | none (`DLCLOSE_MODULE_LIBRARY` only; stated "nothing here hides itself"; header's `FORCE_DENYLIST_UNMOUNT` unused) | — | None present |

Note: no identity library is shipped in the repo (no `compat_library.json`), but the code, tests,
docs and WebUI are built to apply one. Scope is per process and opt-in, which does not change the
classification: D-10 forbids hardware spoofing regardless of scope.

## 4. Zygisk backend audit

**Current implementation.** `libflux_zygisk.so` (arm64/armv7) built from `jni/zygisk/`. Shipped in
`zygisk_provider/` (never loaded); `flux_utility provider enable` copies it to `zygisk/` and needs a
reboot; update/reinstall returns it to off. Kill switch file `no_provider` in the module dir.

**Communication path.**
1. fluxd (`Arming`) writes per-package plans to `compat_provider/plans/<pkg>.json` and a
   `armed.list` (`<package>\t<app_id>\t<tx>`) in the module dir; only with explicit opt-in
   (`compat_zygisk_optin`), lease 24 h, bound to boot id and daemon pid.
2. `preAppSpecialize` (zygote): skips app/webview zygotes, isolated and system UIDs; reads
   `armed.list` via module-dir fd; non-candidates `DLCLOSE` immediately.
3. Candidate → `connectCompanion()` (root companion, Unix socket, bounded read/write, timeout,
   exempted fd). Companion maps UID→package from `/data/system/packages.list`, validates plan,
   boot id, daemon liveness, and decides.
4. `postAppSpecialize` (app sandbox): applies decision (Build fields via JNI, GOT patches for
   props/GL/EGL/Vulkan/dlopen) and reports status; companion writes `compat_provider/proc/<pid>.json`.
5. `ZygiskBackend::verify` succeeds only when the provider reports this plan applied to this PID.

**Provider requirement.** Zygisk (Magisk Zygisk / ZygiskNext / ReZygisk), SDK ≥ 26, module enabled,
user opt-in, reboot after enabling.

**Runtime dependency.** Optional: without it identity layers report `unavailable`; perf/refresh and
the rest of Flux are unaffected (compat failure never blocks performance — `Session.hpp`).

**Security implications.**
- Root companion parses input from app processes → must stay bounded/validated (it is: size caps,
  pid/uid checks). Needs fuzzing before merge.
- In-process GOT patching of graphics/property symbols can crash games or be flagged by anti-cheat
  (account risk to users) even without any bypass.
- `armed.list` in the module dir is readable from zygote context; contains package names only.
- Code violates D-10 today (identity/hardware spoofing). Decision recorded: **Zygisk is only a
  backend**; it must never become a system identity layer, hardware spoof layer, or hidden
  compatibility mechanism. With identity layers removed, the provider currently has **no remaining
  legitimate payload** — it should stay in the tree but be excluded from builds until a
  compatibility-only use (process-scoped, non-identity) is defined.

## 5. Capability schema analysis

Existing: `flux::gfx::CapabilityModel` schema v4 (owned by SynthesisCore per D-11):
domain → key → typed value (bool/int/double/string/list), per-key `source` provenance, collector
`sources[]` with status/detail/timestamp, absent = unsupported/not observed, forward-compatible
parsing, atomic write. `compat::RealHardware` folds it into brand/model/soc/abi/gpu/vulkan/refresh
modes/RAM/SDK/`kernel_gki` (Tri).

| Target Aeyrin capability field (directive §8) | v4 today | Gap |
|---|---|---|
| id, domain | domain/key | reusable |
| current value | value | reusable |
| source | per-key source + sources[] | reusable |
| supported | implied by presence | **missing explicit tri-state** |
| readable / writeable / verified | — | **missing** (Phase 3 probing) |
| interface (sysfs path / binder / HAL) | — | **missing** |
| range | — | **missing** |
| confidence (HIGH/MEDIUM/LOW) | — (compat has its own `Confidence`) | **missing in schema** |
| risk | — | **missing** |
| rollback support | — | **missing** |
| kernel identity (Integration × Generation) | `kernel_gki` Tri only | **missing** (Phase 3) |

Migration requirement: extend additively as v5 (per-fact metadata object alongside `value`),
keep v4 readers working (`from_json` already preserves unknown fields), schema change authored in
SynthesisCore first (D-11), Flux mirror updated after. `RealHardware` is **reusable** as the
"real facts only" view.

## 6. Runtime ownership mapping (no files renamed)

| Current | Component | Target owner |
|---|---|---|
| Flux `Main.cpp` detection, PID/focus, `SessionRuntime`, `GameRuntime`, `LaunchBoost` | lifecycle, session, launch | **Zairenkai Runtime** |
| Flux `PerfPlanner`, `flux_profiler.sh` | memory/touch/storage/CPU policy | **Zairenkai Performance** |
| Flux `flux_refresh`, per-game refresh, `RefreshHold`, `RefreshMatcher` | refresh strategy | **Zairenkai Graphics** |
| Flux `Resolver`, `KnownGameDb`, `GateKind`, unlocked/capable/sustained | requirement analysis | **Zairenkai Compatibility** (analysis only) |
| Flux `Transaction`/`Action`/journal | executor, rollback | **Zairenkai Runtime** (later shared executor, Phase 11) |
| Flux `ZygiskBackend`, `Arming`, `jni/zygisk` | optional backend | **Zairenkai Compatibility backend** (payload blocked) |
| Flux `compat_status.json`, `save_logs` additions | diagnostics | **Zairenkai Observatory** |
| Flux `jni/gfx` model, `Hardware.*`; SynthesisCore providers + schema v4 | context, capability | **Aeyrin** |
| HiCo `hicod` (reads `current_profile`/`gameinfo`) | thermal | **Synrei** — untouched by this branch |

## 7. Integration risk

| Component | Risk | Impact | Recommendation |
|---|---|---|---|
| Identity layers (device/CPU/GPU, props, Build, GL/EGL/Vulkan) | Violates D-10 / directive §18, §60 | Blocks merge | REMOVE OR REDESIGN before any merge |
| Zygisk provider + companion | Root IPC from app processes; in-game hooking; anti-cheat flags | User account risk, crash risk | Keep in tree, exclude from build/package until a non-identity use exists; fuzz companion |
| `Main.cpp` hooks | Touches core loop | Regression in profile selection | Merge only with host tests + device validation (`flux_runtime_validate.sh`) |
| `flux_refresh` rewrite | Single writer; conflicts with future RefreshMatcher split | Refresh fights | KEEP; reconcile in Phase 7 |
| `service.sh` boot capability probe | Runs graphics probe every boot (timeout 20 s) | Boot delay / driver crash contained to child | KEEP; record in Observatory |
| Branch edits to frozen-adjacent files (`Flux.hpp`, `customize.sh`, `flux_utility.sh`) | Conflicts with later phases | Merge conflicts | Integrate before Phase 3 work touches these files |
| `flux_utility.sh` new provider commands | Still `$@` dispatch (B-10) | Expands exec surface | Allowlist in Phase 11 |
| Transaction/journal | Low; well-tested | Positive | Merge candidate |
| Tests (10 files, ~3,200 lines) | Many test identity paths | Would need rewrite after redesign | Keep non-identity tests; mark identity tests with the redesign |
| Docs `GAME_RUNTIME.md`, WebUI strings | Describe identity features | Misleading UI | Update with redesign |

Validation status of the branch: **NOT_TESTED in this audit** (not built or run here).

## 8. Final recommendation

**Merge candidates (after split into a clean integration line, not direct merge):**
`Runtime.*` (Action/Transaction/NodeWrite), `Session.*`, `PerfPlanner.*` + `LaunchBoost`,
`GameProfile` inheritance (perf part), `flux_refresh` per-game target, `Hardware.*`,
`Resolver` analysis/diagnostics (`compat_analyze`), `Main.cpp` session hooks, journal recovery,
`flux_runtime_validate.sh`, related non-identity tests.

**Redesign candidates:** `GameRuntime` (perf path independent of compat), `CompatSettings`/modes
(remove identity profiles; keep `real`/`auto` analysis), `Resolver` gap-closing (diagnose, never
spoof), WebUI `GameRuntimeSection` and `GameSettings.vue` "arm before launch", `GAME_RUNTIME.md`,
Vulkan API clamp.

**Blocked components (REMOVE OR REDESIGN; preserved on branch, not deleted):**
`ProviderPlan` identity items, `Interpose` wrappers (props/GL/EGL/Vulkan), JNI Build-field writes in
`FluxCompatModule`, identity library format, `compat_arm` identity arming.

**Required migration order:**
1. Owner accepts this report.
2. Create a new integration branch from `main`; bring in transaction/journal + session + perf
   planner + launch boost + refresh target + tests (no identity code). Build + host tests.
3. Integrate `Main.cpp` hooks and `service.sh` probe; device-validate with `flux_runtime_validate.sh`.
4. Add Resolver as analysis-only (`compat_analyze`), UI shows unlocked/capable/sustained without
   any apply path for identity.
5. Zygisk provider: keep source, excluded from build, until a compatibility-only design passes
   review (D-10). Never enabled by default.
6. Then Phase 8 (Game Runtime integration into Zairenkai Runtime) and Phase 11 (shared executor).
The original branch `ccr-23925ebb-375wkg` is preserved unchanged throughout.
