# Game Runtime Migration Plan (Phase 1.6 — preparation only)

**Status: PREPARATION COMPLETE. No production code changed. Awaiting owner approval before any
code integration.** Input: `GAME_RUNTIME_INTEGRATION_REPORT.md` (Phase 1.5).

| | |
|---|---|
| Source (read-only, preserved) | `ccr-23925ebb-375wkg` @ `f811dad` |
| Integration branch | `integration/game-runtime-clean` @ `b75491c` (= `main`, pushed, no commits yet) |
| Documentation branch | `ccr-0dc934d1-0a6zta` (this file) |
| Method | Re-create selected files on the integration branch (file-level port + edits), never `git merge` / `cherry-pick` of the old branch |

## 1. CURRENT

`main` has no Game Runtime. The old branch adds 21 files in `jni/compat/`, 8 in `jni/zygisk/`,
10 tests, validation scripts, WebUI section, and edits 16 existing files (report §1).

**Coupling discovered (blocks a pure file copy):**

| Link | Problem |
|---|---|
| `Session.hpp` → `Analyze.hpp` → `Arming.hpp` → `ProviderPlan.hpp` | Session management pulls in provider arming and identity plans. |
| `Runtime.hpp` → `Resolver.hpp` (`Backend` interface takes `Resolution`) | Transaction engine header is tied to the compat resolver and backend abstraction. |
| `GameRuntime` `activate()` = `activate_compat()` then `activate_perf()` | Perf path runs after the identity path. |
| `GameProfile::CompatSettings` | device/cpu/gpu identity profile fields + `IdentityProfile` library. |
| `Main.cpp` hooks use `flux::compat::EndReason` from `Session.hpp` | Fine once Session is decoupled. |

## 2. TARGET

Integration line contains only perf/runtime components; no code path can change what a process
reads about device, CPU or GPU identity. Zygisk not built, not packaged, reported `unavailable`.

| File (old branch) | Action | Target content |
|---|---|---|
| `Runtime.hpp/.cpp` | **KEEP + MODIFY** | `Io`, `Action`, `NodeWrite`, `Transaction`, journal (de)serialise. Remove `Backend`, `ProviderReport`, `Resolver.hpp` include. |
| `Session.hpp/.cpp` | **KEEP + MODIFY** | begin/end, EndReason, one session at a time, `recover()`, journal persist. Drop `Analyze.hpp`; status snapshot without provider fields. |
| `GameRuntime.hpp/.cpp` | **MODIFY** | `activate_perf` / `reassert_perf` / `deactivate` only; no compat activation, no library, no identity. |
| `PerfPlanner.hpp/.cpp` | **KEEP** | memory / touch / storage planners, `LaunchBoost`. |
| `GameProfile.hpp/.cpp` | **MODIFY** | GLOBAL → PRESET → GAME → RUNTIME for `PerfSettings` only; remove `CompatSettings`, `IdentityProfile`, `identities`; unknown `compat` keys in existing JSON are ignored with a warning, never applied. |
| `CompatTypes.hpp/.cpp` | **MODIFY** | Keep `Confidence`, `Tri`, `ContextState`; drop identity `Layer`s/`Mode`s. |
| `Hardware.hpp/.cpp` | DEFER | Real-facts view; needed only for analysis step (later, analysis-only). |
| `Resolver.*`, `Analyze.*` | DEFER (analysis-only redesign) | Not in first integration. |
| `ProviderPlan.*`, `Arming.*`, `ZygiskBackend.*`, `ProviderPaths.hpp` | **REMOVE from integration** | Stay on old branch only. |
| `jni/zygisk/*` (FluxCompatModule, GotHook, Interpose, zygisk.hpp, Android.mk) | **REMOVE from build path** | Not copied; old branch preserved. |
| `docs/GAME_RUNTIME.md` | MODIFY | Rewrite: runtime/perf only; Zygisk section = "unavailable, not built", identity features removed and why (D-10). |
| `scripts/flux_profiler.sh` `flux_refresh` change | **KEEP** | Per-game `FLUX_REFRESH_TARGET_HZ`, unsupported rate ignored. |
| `jni/Profiler.cpp` refresh env | KEEP | |
| `jni/Main.cpp` session hooks | MODIFY | Hooks for perf session + recovery; no compat/provider calls. |
| `jni/FluxCLI.cpp` | MODIFY | No `compat_arm`; `compat_analyze` deferred. |
| `jni/include/Flux.hpp` | MODIFY | Add `COMPAT_PROFILES_FILE` (`game_profiles.json`), `COMPAT_JOURNAL_FILE`, `COMPAT_STATUS_FILE`, `DAEMON_PID_FILE`. Not: `COMPAT_LIBRARY_FILE` identities, `COMPAT_ZYGISK_OPTIN_FILE`. |
| `module/customize.sh` | **Not taken** | The `zygisk_provider/` shipping block is omitted. |
| `module/service.sh` capability probe | DEFER | Only needed by the analysis step. |
| `scripts/flux_utility.sh` | MODIFY | Reports for journal/session only; no `provider enable/disable`. |
| `.github/scripts/compile_zip.sh`, `jni/Android.mk` | MODIFY | Add `jni/compat` sources to `fluxd`; **no** `libflux_zygisk.so`. |
| `CMakeLists.txt` | MODIFY | `flux_compat` with kept files only. |
| `webui` GameRuntimeSection / store / GameSettings | MODIFY | Per-game perf profile + session state; remove identity UI and "arm before launch"; provider shown as "Unavailable". |
| `tests/session_test`, `compat_test` (perf parts), `refresh_script_test.sh` | KEEP/MODIFY | Remove identity cases. |
| `tests/provider_test`, `zygisk_backend_test`, `fluxd_companion_test`, `interpose_test`, `got_hook_test`, `got_*.c` | **Not taken** | Test removed code; preserved on old branch. |
| `scripts/flux_runtime_validate.sh` | MODIFY | Drop provider checks. `flux_provider_validate.sh`, `tools/compat-testapp/` not taken. |
| `NOTICE.md` / `changelog.md` edits | Re-author | Only for what is actually integrated. |

**Explicitly removed from the integration line:** device, CPU and GPU identity substitution; Vulkan
identity rewriting (deviceName/vendorID/deviceID/driverVersion/apiVersion); `ro.*` property
interception; `android.os.Build` / fingerprint field writes; GL/EGL string wrappers; GOT hooking;
provider arming, companion, `armed.list`.

## 3. MIGRATION STEPS (each a separate reviewed commit on `integration/game-runtime-clean`)

1. **Transaction engine**: port `Runtime.*` without `Backend`/`Resolver`; add to CMake; port
   Action/Transaction tests. Gate: host tests green.
2. **Perf planner + launch boost**: port `PerfPlanner.*` unchanged; tests.
3. **Profile inheritance (perf only)**: port `GameProfile.*` minus identity; tests for chain,
   cycles, unknown presets, ignored `compat` keys.
4. **GameRuntime (perf lifecycle)**: new `GameRuntime` with activate_perf/reassert/deactivate.
5. **Session + journal + recovery**: port `Session.*` without `Analyze`; crash-recovery tests
   (journal replay, partial failure keeps journal).
6. **Refresh target**: port `flux_refresh` change + `Profiler.cpp` env; `refresh_script_test.sh`.
7. **Daemon wiring**: `Main.cpp` hooks, `Flux.hpp` paths, `FluxCLI`, `Android.mk`, `compile_zip.sh`.
   Frozen contracts unchanged (diff check). Full host tests + CI Android build.
8. **WebUI + docs + utility**: per-game perf UI, provider "Unavailable", rewritten `GAME_RUNTIME.md`,
   `flux_utility` reports, locales. WebUI build.
9. **Device validation** with modified `flux_runtime_validate.sh` (owner device).
10. Integration report → owner approval → PR to `main`.

Analysis-only compatibility (Hardware/Resolver/Analyze, `compat_analyze`) is a later, separate step.

## 4. RISKS

| Risk | Impact | Mitigation |
|---|---|---|
| Decoupling Session/Runtime from Resolver changes tested code | Regressions in rollback | Port tests first; verify journal format unchanged |
| Journal format drift vs old branch | Old journals on devices that ran the old branch not recoverable | Recover must accept old `NodeWrite` lines, ignore identity lines with a warning |
| Existing `game_profiles.json` with `compat` identity keys (old-branch users) | Silent behaviour change | Parse, ignore, log "identity override not supported"; never apply |
| Leftover `zygisk/` folder from old-branch installs | Provider still loaded by Zygisk | Installer removes `zygisk/`, `zygisk_provider/`, `armed.list` of the old build (module-dir files only, recorded) and reports it |
| `Main.cpp` hook placement | Profile selection regression | Host tests + device validation of every profile transition |
| Refresh single-writer vs `RefreshHold`/`RefreshMatcher` | Refresh fights | Keep old ownership marker; Phase 7 resolves B-12 |
| `flux_utility.sh` `$@` exposure | Security (B-10) | No new functions exposed beyond reports |
| Android build untestable here (B-09) | Build breaks found late | CI Android build required per step |

## 5. TEST REQUIREMENTS

| Level | Required before merge |
|---|---|
| Host (CMake/ctest) | Existing 7 tests + transaction, perf planner, launch boost (deadline/cancel/restore), profile inheritance, session idempotency, journal recovery (crash mid-apply, partial restore), identity keys ignored, old-journal compatibility |
| Static | `grep` gate in CI: no `SetStaticObjectField`, `__system_property_get` wrappers, `glGetString`/`vkGetPhysicalDeviceProperties` interception, `zygisk` in build files |
| Shell | `refresh_script_test.sh`, shellcheck on changed scripts |
| Build | CI `ndk-build` (arm64, arm), 3 zip flavors; zip contains no `libflux_zygisk.so` |
| WebUI | `bun run build`; provider shown "Unavailable" |
| Device (owner) | session start/end, launch boost, per-game memory/touch/storage, refresh target, kill daemon mid-session → recovery restores, HiCo still follows `current_profile`/`gameinfo` |
| Reporting | Any untested item stays NOT_TESTED |
