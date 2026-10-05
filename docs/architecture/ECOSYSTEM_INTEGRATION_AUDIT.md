# Zairenkai Ecosystem Integration Audit (Phase 4.5D)

This is a cross-repository audit after the public brand migrations (4.5, 4.5B and 4.5C). It is
read and verify first; two corrective fixes were made. No device validation is claimed.

## 1. Commits audited

| Repository (public identity) | Branch | Audited commit | Migration commit | Tree |
|---|---|---|---|---|
| Flux (Zairenkai) | `integration/game-runtime-clean` | `5f6135a` | `5f6135a` (4.5) | clean |
| HiCo (Synrei Thermal Intelligence) | `ccr-0dc934d1-0a6zta` | `b7f7845c` | `b7f7845c` (4.5B) | clean |
| SynthesisCore (Zairenkai Intelligence) | `ccr-0dc934d1-0a6zta` | `2fe71b6` | `2fe71b6` (4.5C) | clean |

`main` was not used, merged or modified in any repository.

## 2. Contract map

| Source | Target | Contract | Technical identifier | Consumer | Expected behavior | Status | Risk |
|---|---|---|---|---|---|---|---|
| Zairenkai | Synrei | Thermal state file | `/dev/hico/state` (`kSynreiStatePath`, `HICO_STATE_FILE`) | `SynreiThermalAdapter` | Key=value file, removed when `hicod` stops | **OK**: path identical in both repos | Low |
| Zairenkai | Synrei | State keys | `state`, `reason`, `updated`, `pid`, `cpu_temp`, `gpu_temp`, `battery_temp` | `SynreiThermalAdapter` | Every key read is written by `Daemon.cpp` | **OK** (checked per key) | Low |
| Zairenkai | Synrei | State values | `idle`, `boost`, `relaxed`, `safety`, `suspended`, `disabled` | adapter mapping (safety → constrained, boost → unconstrained, else unknown) | Unchanged strings | **OK** | Low |
| Zairenkai | Synrei | Module presence / ownership | `/data/adb/modules/hico/{module.prop,disable,remove}` | `flux_profiler.sh` (thermal ownership) | Synrei owns thermal when installed and enabled | **OK**; module ID `hico` frozen | Low |
| Zairenkai | Synrei | Config dir | `/data/adb/.config/hico` | `flux_profiler.sh`, `flux_utility.sh` | Read only | **OK** | Low |
| Zairenkai | Synrei | Commands | `hicod status`, `zones`, `device`, `restore` | `flux_utility.sh` (report), `uninstall.sh` (restore) | Same command names and output | **OK**; dispatch unchanged; only the help banner changed | Low |
| Synrei | Zairenkai | Game / profile signals | `/data/adb/.config/flux/{gameinfo,current_profile,.lock,synthesis_core.json}`, `/data/adb/modules/flux`, `fluxd` | HiCo `FluxLink` | Same paths written by fluxd | **OK** | Low |
| Synrei | Zairenkai | Minimum version | `FLUX_MIN_VERSION_CODE 46` vs Zairenkai versionCode (git commit count, currently 162) | `customize.sh` gate | Version continuity kept | **OK** (no version reset) | Low |
| Zairenkai | Zairenkai Intelligence | Launch | `com.febricahyaa.synthesiscore.MainKt` via `app_process` | `module/service.sh` | Same class and package | **OK** | Low |
| Zairenkai | Zairenkai Intelligence | Modes | `--version`, `--resolve`, `--once`, `--capabilities` | `service.sh` (`--version` last line, `--resolve`) | Unchanged | **OK**; `--version` prints `Protocol.VERSION` only | Low |
| Zairenkai | Zairenkai Intelligence | Output contract | `synthesis_version 3` and 14 field names | `jni/base/SynthesisCore` reader, WebUI monitor, HiCo `FluxLink` (via `synthesis_core.json`) | Every field read exists in `Protocol.kt` | **OK** (checked per field) | Low |
| Zairenkai Intelligence | Zairenkai | Release assets | `SynthesisCore-<tag>.apk`, `.sha256`, `.cert.sha256` | `sync_synthesiscore.sh` | Same names | **OK** | Low |
| Zairenkai Intelligence | Zairenkai | Sync event | `synthesiscore-release` → `FebriCahyaa/Flux` | `sync_synthesiscore.yml` | Same event type and target | **OK** | Low |
| Zairenkai Intelligence | Zairenkai | Signing pin | `prebuilt/synthesiscore.cert.sha256` = `synthesiscore.json` `certificate_sha256` | sync, install verify | Same certificate | **OK**; the APK sha256 matches the tracked checksum and manifest | Low |
| All | — | Update channels | Flux `…/Flux/main/update.json`; HiCo `…/HiCo-Release/main/update.json` | root managers | Unchanged | **OK** | Low |

`tests/ecosystem_contract_check.sh` checks every row: 95 checks, all passed.

## 3. Defects found and fixed

1. **HiCo CI integrity manifest (real defect from 4.5B).**
   - **Problem:** `tools/verify_webui.py` compares `webui/` with `docs/integrity/webui.sha256`. The 4.5B label edits changed 5 WebUI source files, so this CI step would have failed.
   - **Fix:** the manifest was regenerated with the repository's own `verify_webui.py --write`, as earlier WebUI changes did in commit `58cc1213`.
   - **Result:** exactly those 5 hashes changed, and 108 files verify.
2. **Brand tests pinned to a fixed base commit (latent CI defect from 4.5B/4.5C).**
   - **Problem:** `HiCo/tests/synrei_brand_test.sh` and `SynthesisCore/tests/brand_migration_test.sh` compared everything against the pre-migration commit, so any later legitimate change would have failed CI.
   - **Fix:** the history comparison now runs only when `BRAND_AUDIT_BASE` is set. Both tests pass with and without it.

## 4. Public branding

| Surface | Zairenkai | Synrei | Zairenkai Intelligence |
|---|---|---|---|
| README | "# Zairenkai" plus identity table | `README.md` is a preserved historical phase document (D-12); identity is in `SYNREI_BRAND_MIGRATION.md` | "# Zairenkai Intelligence" plus identity section |
| Module / app metadata | `name=Zairenkai` | `name=Synrei Thermal Intelligence` | `app_name` "Zairenkai Intelligence" (source) |
| CLI help | "Zairenkai CLI (Flux Tweaks runtime)", `zairenkai` alias | "Synrei Thermal Intelligence … (HiCo backend)" | "Zairenkai Intelligence (SynthesisCore backend)" |
| WebUI source | title "Zairenkai" | title, Home, About and description | n/a |
| Release title | CI unchanged (unrelated CI; B-39) | "Synrei Thermal Intelligence $TAG" | "Zairenkai Intelligence $TAG" |

**Aeyrin** appears only in internal or migration documents:
- `BRAND_MIGRATION.md`, `CAPABILITY_MODEL.md`, `KERNEL_INTELLIGENCE.md`;
- `Brand.hpp` (`kCodename`, internal);
- a `CapabilityBootstrap.hpp` comment;
- SynthesisCore docs and its brand test.

It is in no README, help text, module metadata, WebUI or release title; the contract check covers 14 public files.

**Known gap (B-41, new):** Synrei's public text still names the platform by its legacy names,
and much WebUI copy still says "HiCo". Examples:
- `module.prop` description "powered by Flux. Requires Flux Tweaks.";
- `customize.sh` ecosystem gate;
- the suspended notification;
- WebUI strings such as "Waiting for Flux Tweaks" and "Works with Flux Tweaks";
- WebUI copy that says "HiCo" in many strings.

These are human-readable and do not affect any contract. A label pass is an owner decision; 4.5B scoped only the HiCo → Synrei titles.

## 5. Legacy identifier classification

| Repository | Name searched | Registry / where classified |
|---|---|---|
| Zairenkai | flux, hico, synthesiscore (any case) | `docs/architecture/brand_identifiers.tsv`, enforced by `brand_inventory_test` |
| Synrei | hico, hicod (any case) | `HiCo/docs/architecture/hico_identifiers.tsv`, enforced by `synrei_brand_test.sh` |
| Zairenkai Intelligence | synthesiscore, "Synthesis Core" | `SynthesisCore/docs/architecture/synthesiscore_identifiers.tsv`, enforced by `brand_migration_test.sh` |
| Synrei | flux (41 files) | **runtime contract** in `jni/` (`FluxLink`, `FLUX_*` paths, `flux` command); **compatibility** in `module/` (Flux gate, release URL); **intentionally preserved legacy identifier** in WebUI copy (B-41); **legal attribution** in `EULA.md`, `NOTICE.md`; **historical documentation** in `changelog.md`, `docs/`; **test fixture** in `tests/`; **release/infrastructure** in `.github/` |
| Synrei | synthesiscore | `docs/architecture/REPOSITORY_DATA_INVENTORY.md`: historical documentation |
| Zairenkai Intelligence | flux | **runtime contract**: `Protocol.kt` and `AtomicFile.kt` comments name the Flux reader; `release.yml` `FLUX_REPOSITORY`/`FLUX_DISPATCH_TOKEN` (release/infrastructure); `README.md` consumer names; `MainKtTest.kt` (test fixture); docs (historical / migration documentation) |
| Zairenkai Intelligence | hico | docs only: historical / migration documentation |
| All | Aeyrin | migration documentation or internal codename (see section 4) |

No reference was deleted.

## 6. WebUI source vs artifact (B-40, new)

- **Status:** Synrei `webui/` source says Synrei, but the tracked `module/webroot` bundle still shows "HiCo Thermal".
- **Rebuild verified in a scratch copy:** `bun install --frozen-lockfile && bun run build`, the repository's own Vite config, which outputs to `../module/webroot`. It succeeds and the new bundle contains "Synrei Thermal Intelligence".
- **Why it was not committed:** the build replaces the two hashed files (`index-*.css`, `index-*.js`), which deletes tracked files. Under the data preservation lock this needs the normal WebUI release step and owner approval.
- **Zairenkai:** its own WebUI build is unchanged.
- **Zairenkai Intelligence:** the APK label shows only after the next signed release plus the Zairenkai prebuilt sync. The tracked `prebuilt/synthesiscore.apk` (v2.1.1) still contains "Synthesis Core", which is not visible because the APK is never installed.

## 7. Release and update chain

**Zairenkai Intelligence:**
- source → `release.yml` signs with the existing certificate → `SynthesisCore-<tag>.apk` (+ `.sha256`, `.cert.sha256`);
- → `repository_dispatch synthesiscore-release` to `FebriCahyaa/Flux`;
- → `sync_synthesiscore.yml` / `.sh` (asset lookup by name, certificate pin) → `prebuilt/`.
- No identifier in the chain changed; only the GitHub release title did.

**Synrei:**
- source → `release.yml` → `HiCo-Release` channel (`updateJson`).
- Module ID `hico` and the update URL are unchanged.

**Zairenkai:**
- `updateJson` and the per-flavor rewrite in `compile_zip.sh` are unchanged.
- Module ID `flux`; versionCode continues from the git commit count (no reset).

## 8. Configuration and upgrade model

| Upgrade | Stable identifiers | Data kept | Uninstall | Device evidence |
|---|---|---|---|---|
| Flux install → Zairenkai-branded update | `id=flux`, `fluxd`, `/data/adb/modules/flux`, `/data/adb/.config/flux` | Config untouched; telemetry and `installation.json` under `/data/adb/.config/zairenkai` untouched (no epoch reset) | Removes only the symlinks it created (`zairenkai` added) | NOT_TESTED (B-39) |
| HiCo install → Synrei-branded update | `id=hico`, `hicod`, `/data/adb/.config/hico`, `/dev/hico` | Config untouched; `hico.conf` format unchanged | Unchanged (`rm -rf /data/adb/.config/hico /dev/hico` as before) | NOT_TESTED |
| SynthesisCore deployment → Zairenkai Intelligence release | package, certificate, asset names | No state of its own (paths given by fluxd) | n/a | NOT_TESTED (needs a signed release) |

No configuration is migrated. Zairenkai's `ConfigNamespace` helper exists but is not called.

## 9. Tests

| Repository | Suite | Result |
|---|---|---|
| Zairenkai | host `ctest` (includes `brand_migration_test`, `brand_inventory_test`, `policy_decision_test`, `policy_executor_test`, `live_policy_test`) | **35/35 PASS** |
| All | `tests/ecosystem_contract_check.sh` | **95/95 PASS** |
| Synrei | `ctest` (`unit`, `cli`, `synrei_brand`) | **3/3 PASS** |
| Synrei | Python `tests/*_test.py` | **20/21 PASS**: `ingest_test` fails identically at the pre-migration base (PyYAML parses a value as `True`; the test expects `'true'`) |
| Synrei | `tools/verify_webui.py` | PASS (108 files) after the manifest fix |
| Zairenkai Intelligence | `tests/brand_migration_test.sh` | PASS (default and `BRAND_AUDIT_BASE=7b6e1cd`) |
| Zairenkai Intelligence | Gradle `testDebugUnitTest` | **Not run**: no Android SDK in this environment |

CI: not checked in any repository.

## 10. Device validation status

No device validation has occurred in this audit.

| Blocker | Status |
|---|---|
| B-26 | Partly resolved (host); device validation OPEN |
| B-33A | Resolved except device overhead; device OPEN |
| B-33B | Resolved for the interface; device freshness OPEN |
| B-33C | OPEN (threshold calibration on devices) |
| B-34C | OPEN (device session-end validation) |
| B-35 | DEFERRED (owner decision) |
| B-36 | OPEN (device validation of telemetry analysis) |
| B-37 | Partly resolved (host only); device OPEN |
| B-38 | Resolved on host; device execution depends on B-37 |
| B-39 | OPEN: `zairenkai` alias / upgrade on device, config lookup decision, website/CI/NOTICE copy, repository renames |
| B-40 | NEW, OPEN: Synrei `module/webroot` needs the normal WebUI rebuild (verified feasible); SynthesisCore APK label needs the next signed release |
| B-41 | NEW, OPEN: Synrei public copy still uses the legacy platform name (Flux Tweaks) and "HiCo" in WebUI strings |

## 11. Verdict

The cross-repository technical contracts are consistent; there is no broken path, command, field, asset,
event, pin or update URL. **Phase 4.5 brand migration: COMPLETE WITH RELEASE ARTIFACT / DEVICE
VALIDATION PENDING** (B-39, B-40, B-41).
