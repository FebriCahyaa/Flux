# Validation Log

Status values: PASS, FAIL, NOT_TESTED, UNAVAILABLE. Host or CI success is never reported as device
success.

## Phase 0 baseline — 2026-09-29

Environment: cloud container, Linux x86_64, CMake + GCC/Clang, Python 3, Bun. No Android NDK,
Android SDK, JDK 25 toolchain for AGP 9, or device.

| Check | Repo @ commit | Command | Result |
|---|---|---|---|
| Host tests (7) | Flux `main` @ `b75491c` | `git submodule update --init jni/external/rapidjson`; `cmake -S . -B <build>`; `cmake --build`; `ctest` | PASS (7/7: refresh_hold, render_booster, capability_model, capability_schema, vulkan_probe, platform_probe, synthesis_core) |
| WebUI build | Flux `main` @ `b75491c` | `cd webui && bun install --frozen-lockfile && bun run build` | PASS |
| Android build (`ndk-build`, zip) | Flux | — | NOT_TESTED (no NDK here; CI `build.yml` covers it) |
| Host tests (ctest) | HiCo `main` @ `a61cad0` | `cmake -S . -B <build>`; `cmake --build`; `ctest` | PASS (2/2: hico_tests, cli) |
| Device DB up to date | HiCo | `python3 tools/gen_device_db.py --check` | PASS |
| Python suites (15) | HiCo | `python3 tests/<suite>.py` for the 15 suites in `build.yml` (`hico_tools_test` with `HICOD=<build>/hicod`) | PASS (15/15) |
| Evidence/database validators in `build.yml` (`hico_evidence_audit --strict`, `hico_evidence verify`, `hico_collector verify`, `hico_generator verify`, `hico_database validate`, `verify_webui`) | HiCo | — | NOT_TESTED in Phase 0 (CI covers them) |
| Android build | HiCo | — | NOT_TESTED |
| Unit tests (`testDebugUnitTest`) | SynthesisCore `master` @ `7cbdda2` | — | NOT_TESTED (no Android SDK / JDK 25) |
| Any on-device behaviour (daemon, profiles, HiCo unlock/restore, provider, sessions) | all | — | NOT_TESTED (no device) |

Working trees were clean after every check (`git status --short` empty); build output went to the
session scratchpad, `webui/dist` and `node_modules` are git-ignored.

## Phase 1 — 2026-09-29
Documentation only; no build inputs changed. Evidence commands: web search, `curl` to
registry.npmjs.org / pypi.org / crates.io (404 = unclaimed), `dns.google/resolve` NS lookups,
GitHub repository search. Trademark databases: NOT_TESTED (B-14).

## Game Runtime Step 1 — 2026-09-29 (`integration/game-runtime-clean` @ `b604eac`)
| Check | Result |
|---|---|
| Tests written before implementation; CMake configure failed without `Transaction.cpp` (red) | observed |
| Host ctest (7 existing + `transaction_test`) with `-Werror` | PASS 8/8 |
| `transaction_test` scenarios: snapshot, apply, verify success, verify failure, apply failure, journal-sink failure, rollback, restore/reapply, restore failure, crash recovery (+ legacy journal, non-sticking restore), corrupted journal (malformed, relative, `..`, empty path, unknown version, empty) | PASS |
| Isolation grep (Resolver/Backend/ProviderPlan/Arming/zygisk/identity/ro./GL/Vulkan) on `jni/runtime`, test | CLEAN |
| `fluxd` device build | unchanged (engine not in `Android.mk`); CI NOT_TESTED |
| Device | NOT_TESTED |

## Game Runtime Step 1 closure — 2026-09-29 (`integration/game-runtime-clean` @ `8fc92c0`)
CI run https://github.com/FebriCahyaa/Flux/actions/runs/36617841919 (workflow_dispatch): success.
| Check | Result |
|---|---|
| Forbidden symbol check (CI step) | PASS; local negative test with planted `flux_wrap_` → exit 1 |
| Host tests in CI | PASS 8/8 incl. `transaction_test` |
| `ndk-build` r29 arm64-v8a + armeabi-v7a, `FluxRuntime <= Transaction.cpp` with `-Werror`, `fluxd` linked | PASS |
| WebUI build in CI | PASS |
| Packaging (arm64, arm, universal zips) | PASS |
| Transaction engine called at runtime | not yet (by design) |
| Device | NOT_TESTED |
Step 1: **CLOSED**.

## Game Runtime Step 2 — 2026-09-29 (`integration/game-runtime-clean` @ `98a7a25`)
| Check | Result |
|---|---|
| Tests written first; CMake configure failed without implementation (red) | observed |
| Host ctest incl. `performance_planner_test` (-Werror) | PASS 9/9 |
| Scenarios: profile→plan, zero writes while planning, invalid selection rejected, unsupported node/refresh, mitigation block, interface rejection (`..`), execute+restore via Transaction, transaction failure rollback, launch-boost timeout, duration clamp, one-at-a-time, cancellation (journal emptied), nothing-available, failing-write rollback | PASS |
| Forbidden-symbol check | clean |
| CI run https://github.com/FebriCahyaa/Flux/actions/runs/36619381182 (workflow_dispatch) | success: host tests, ndk-build arm64+arm with FluxPerf, WebUI, packaging |
| Daemon call path / device | none by design / NOT_TESTED |

## Game Runtime Step 3 — 2026-09-29 (`integration/game-runtime-clean` @ `3410fbd`)
| Check | Result |
|---|---|
| Tests written first; CMake configure failed without implementation (red) | observed |
| Host ctest incl. `profile_model_test` (-Werror) | PASS 10/10 |
| Scenarios: inheritance order, override priority, missing parent, unknown profile, cycle/self-cycle, invalid field/value/type/custom Hz, invalid document, explanation text, legacy game_profiles, legacy library, gamelist lite_mode | PASS |
| Forbidden-symbol check | clean |
| CI run https://github.com/FebriCahyaa/Flux/actions/runs/36620727531 | success: host tests, ndk-build arm64+arm (FluxPerf with rapidjson), WebUI, packaging |
| Device file loading / daemon call path / device | not implemented / not implemented / NOT_TESTED |

## Game Runtime Step 4 — 2026-09-29 (`integration/game-runtime-clean` @ `c37ef15`)
| Check | Result |
|---|---|
| Location decision documented before implementation (`af4643d`) | done |
| Tests written first; CMake configure failed without implementation (red) | observed |
| Host ctest incl. `game_runtime_test` (-Werror) | PASS 11/11 |
| Scenarios: start applies profile (+ write-ahead journal, idempotent), no-profile game unchanged, exit restores (+ reapply after script overwrite, journal removed), process death rollback incl. launch boost, boost deadline, daemon restart recovery (+ legacy compat_journal, unrecoverable journal kept), transaction failure fallback, resolve failure (unknown profile, invalid JSON) fail closed, multi-game switch and pid restart, compatibility fields ignored, gamelist lite fallback | PASS |
| Forbidden-symbol check | clean |
| CI run https://github.com/FebriCahyaa/Flux/actions/runs/36621945822 | success: host tests, ndk-build arm64+arm, WebUI, packaging |
| fluxd call path / device | not implemented / NOT_TESTED |

## Game Runtime Step 4 wiring — 2026-09-29 (`integration/game-runtime-clean` @ `dd86b7c`)
| Check | Result |
|---|---|
| Tests first (runtime_host_test red at configure) | observed |
| Daemon lifecycle test: fluxd start → recover journal → game start event → runtime active → repeat pass idempotent → reapply → game exit → restore (journal removed) → disable_tweaks | PASS |
| Refresh bridge: supported 90 Hz requested; 144 Hz on 60/90/120 panel ignored; `dumpsys display` parsing (119.99→120) | PASS |
| Refresh shell test (`flux_refresh`: target honoured, unsupported ignored, restore, garbage ignored, double boost) | PASS |
| Adapter test (real temp dir): missing node skipped, virtual/removable block devices excluded, apply+restore on real files, rollback on ENOSPC write, atomic FileStore, symlink target refused | PASS |
| Host ctest total | PASS 13/13 |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36623380177 (ndk-build of Main.cpp/Profiler.cpp/GameRuntimeHost.cpp, arm64+arm, packaging) | success |
| Device | NOT_TESTED |

## Game Runtime Step 4 final review — 2026-09-29 (`integration/game-runtime-clean` @ `3e96312`)
| Area | Result |
|---|---|
| Main.cpp lifecycle integration (diff review) | OK |
| Journal recovery path | OK after fix: legacy `#flux-compat-journal v1` header now parsed; test added (red -> green) |
| Game start activation / game exit restore / process death restore / daemon restart recovery | PASS (host tests) |
| Refresh bridge (C++ + shell) | PASS |
| Adapter safety (real temp files) | PASS |
| Rollback behaviour | PASS |
| Forbidden-symbol gate | clean |
| Host ctest | PASS 13/13 |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36624510915 | success (host, ndk-build arm64+arm, WebUI, packaging) |
| Device validation | **NOT_TESTED** |
Step 4: **IMPLEMENTED**.

## Game Runtime Step 5 — 2026-09-29 (`integration/game-runtime-clean` @ `7e35fba`)
| Check | Result |
|---|---|
| Tests written first; CMake configure failed without implementation (red) | observed |
| session begin, duplicate begin (same process no-op; restarted process / other game → switch, reverse end), session end, duplicate end (no participant called), process death reason, daemon restart recovery (once; implicit before first begin), cleanup ordering (reverse; failing participant does not block restore), forwarding only while active | PASS |
| Two tests initially assumed no recovery call before begin; implementation correctly recovers first — tests updated to the daemon order | noted |
| Host ctest | PASS 14/14 |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36630954964 (ndk-build of Main.cpp, SessionHost.cpp, FluxSession) | success |
| sessions.json format | unchanged (no code change in SessionRecorder) |
| Device | NOT_TESTED |

Step 5: **IMPLEMENTED**, architecture approved by owner (2026-09-29). Device validation: **NOT_TESTED** (B-25).

## Step 6 Observatory foundation — 2026-09-29 (`integration/game-runtime-clean` @ `b82761e`)
| Check | Result |
|---|---|
| Tests written first; CMake configure failed without implementation (red) | observed |
| event creation (registry, validate, store assigns id/sequence), serialization round-trip (escapes, snapshots), invalid event rejection (source, reason, unknown type, disallowed source, missing session/transaction id, invalid severity/confidence/result, rejected not stored), timestamp validation (0, negative, 1970, far future, small skew, non-integer), ordering (timestamp then write sequence, filters, limit, bounded drop), corrupted handling (empty, invalid JSON, array, missing fields, truncated, unknown key, schema 9, JSONL line reporting) | PASS |
| Host ctest | PASS 15/15 |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36632946443 (ndk-build incl. FluxObservatory) | success |
| sessions.json / session_live.json | unchanged |
| Producers / device | none wired / NOT_TESTED |

## Step 6.5 Observatory integration bridge — 2026-09-29 (`integration/game-runtime-clean` @ `aa88ce2`)
| Check | Result |
|---|---|
| Tests written first; CMake configure failed without bridge implementation (red) | observed |
| event on session start (+ runtime/transaction events carry session id, ordering BEGIN<APPLY<VERIFY<PROFILE_APPLIED<RUNTIME_ACTIVATE<SESSION_START, switch) | PASS |
| event on restore (TRANSACTION_RESTORE before/after evidence, PROFILE_RESTORED, RUNTIME_RESTORE, SESSION_END process_death, context cleared) | PASS |
| transaction failure event (TRANSACTION_APPLY failed, TRANSACTION_ROLLBACK, RUNTIME_FAILURE, no PROFILE_APPLIED) | PASS |
| recovery events (START<SUCCESS, FAILED with journal kept, SUCCESS when none) | PASS |
| Observatory unavailable (null / throwing / rejecting sink; throwing observers): node values, journals, states identical to a working Observatory | PASS |
| emission after transition (PROFILE_APPLIED observed with node already applied); no event rejected by validation | PASS |
| Existing tests unchanged and passing (transaction, planner, profile, runtime, host, session) | PASS |
| Test bug found and fixed: dangling reference to a temporary query result in CHECK_EQ | noted |
| Engine finding B-27 (rollback reported incomplete for a node whose write failed) | recorded, not changed |
| Host ctest | PASS 16/16 |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36634896135 (ndk-build incl. FluxBridge, ObservatoryHost) | success |
| flux.log / sessions.json / session_live.json | unchanged |
| Device | NOT_TESTED |

## Step 6.5.1 restore verification — 2026-09-30 (`integration/game-runtime-clean` @ `a121b2c`)
| Check | Result |
|---|---|
| Tests written first; transaction_test failed to compile without restored/failed counts (red) | observed |
| restore write succeeds (RESTORED, counts 1/0) | PASS |
| restore write fails but state matches snapshot (rollback SUCCESS, journal empty) | PASS |
| restore write fails and state differs (RESTORE_FAILED, journal keeps exactly that node); non-sticking write; unreadable node | PASS |
| partial transaction recovery (3 entries: restored, already-at-original despite write failure, differing → 2/1, journal kept); unreadable node at recovery | PASS |
| journal handling (clean → empty; failed → only unverified entry; later recovery clears it) | PASS |
| Bridge mapping SUCCESS/PARTIAL/FAILED → ok/partial/failed; B-27 scenario now rollback ok and journal removed | PASS |
| Test fake corrected: writes no longer create missing nodes (matches sysfs and real adapter) | noted |
| Host ctest | PASS 16/16 |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36665746650 | success |
| Device | NOT_TESTED |

## Step 7 — Kernel Intelligence foundation (`9e19e1a`)

| Check | Result |
|---|---|
| Tests written first; CMake configure failed without `KernelIntelligence.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 17/17 |
| clang-18 `-Wall -Wextra -Wpedantic -Werror` syntax check of module + test | PASS |
| Read-only: real temp tree content and mtime unchanged after probing; `ReadOnlyFs` has no write API | PASS |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36667540789 | success (host, forbidden-symbol gate, ndk-build arm64+arm, WebUI, zips) |
| Device | NOT_TESTED |

## Step 7.5 — Capability context bridge (`468cf39`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `CapabilityContext.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 18/18 |
| clang-18 -Werror syntax check (context, kernel, test) | PASS |
| Planner plan identical with and without context | PASS |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36668216151 | success |
| Device | NOT_TESTED |

## Step 7.6 — Runtime capability bootstrap (`0b5a7e4`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `CapabilityBootstrap.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 19/19 |
| clang-18 -Werror syntax (bootstrap + test) | PASS |
| Daemon glue (`CapabilityHost.cpp`, Main.cpp, GameRuntimeHost.cpp) | CI ndk-build only (spdlog submodule not present locally) |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36669240984 (incl. ndk-build of the daemon glue) | success |
| Device | NOT_TESTED |

## Step 8 — Graphics Intelligence (`3af9cc0`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `GraphicsIntelligence.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 20/20 |
| clang-18 -Werror syntax (module + test) | PASS |
| Daemon glue (`CapabilityHost.cpp`) | CI ndk-build only |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36669963849 | success |
| Device | NOT_TESTED |

## Step 8.5 — Display & rendering capability (`365c8df`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `DisplayIntelligence.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 21/21 |
| clang-18 -Werror syntax (module + test) | PASS |
| Daemon glue (`CapabilityHost.cpp`) | CI ndk-build only |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36670838946 | success |
| Device | NOT_TESTED |

## Step 8.6 — Bottleneck observation (`920cb88`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `BottleneckModel.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 22/22 |
| clang-18 -Werror syntax (module + test) | PASS |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/36674890357 | success |
| Device | NOT_TESTED (no call path) |

## Step 8.7 — Runtime metrics collector (`e0e4bdd`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `RuntimeMetrics.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 23/23 |
| clang-18 -Werror syntax (module + test) | PASS |
| Read-only: fake tree unchanged after sampling | PASS |
| Forbidden-symbol check | clean |
| CI https://github.com/FebriCahyaa/Flux/actions/runs/37267128969 | success |
| Device | NOT_TESTED (no call path) |

## Step 8.8 — Runtime metrics sampling lifecycle (`d5194ac`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `RuntimeMetricsSampler.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 24/24 |
| clang-18 -Werror syntax (sampler + test) | PASS |
| Daemon glue (`SessionHost.cpp`) | CI ndk-build only (not monitored) |
| Forbidden-symbol check | clean |
| CI | triggered, not monitored (owner instruction) |
| Device | NOT_TESTED |

## Step 8.8.1 — FPS observation bridge (`d42b866`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `FpsObservation.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 25/25 |
| clang-18 -Werror syntax (bridge, sampler, test) + SessionRecorder.hpp include check | PASS |
| SessionRecorder change additive only (diff: +publish, +clear on stop, +const accessor, 0 lines removed) | reviewed |
| SessionRecorder runtime behaviour | no host harness (spdlog + device /sys); CI ndk-build only, device NOT_TESTED |
| No second FPS loop (reads == samples taken, none after stop) | PASS |
| Forbidden-symbol check | clean |
| CI | triggered, not monitored (owner instruction) |
| Device | NOT_TESTED |

## Step 8.9 — Synrei thermal context (`7007422`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `ThermalContext.cpp` / `SynreiThermalAdapter.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 26/26 |
| clang-18 -Werror syntax (thermal, sampler, test) | PASS |
| Zero thermal writes: fake tree unchanged, reads limited to `dev/hico/state` + `proc/<pid>`; real tree mtimes unchanged | PASS |
| GameRuntime / SessionRecorder / FPS architecture diff | none |
| Forbidden-symbol check | clean |
| CI | triggered, not monitored (owner instruction) |
| Device | NOT_TESTED |

## Step 8.10 — Bottleneck result integration (`3ae57cd`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `BottleneckResult.cpp` / `BottleneckEvents.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 27/27 (existing observatory tests unchanged and passing with the two added types) |
| clang-18 -Werror syntax (result, events, sampler, test) | PASS |
| Protected files (SessionRecorder, perf, GameRuntimeHost, runtime, kernel, thermal) diff | none |
| Forbidden-symbol check | clean |
| CI | triggered, not monitored (owner instruction) |
| Device | NOT_TESTED |

## Step 8.11 — Observatory persistent storage + retention (`1ee1c7a`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `TelemetryStore.cpp` / `InstallationEpoch.cpp` (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 28/28 (existing observatory/bridge tests pass with the new category and types) |
| clang-18 -Werror syntax (store, epoch, events, test) | PASS |
| CLI `telemetry` handler and `ObservatoryHost.cpp` compiled against stubs (spdlog/properties) | PASS; CLI run reports "telemetry directory absent" on a host without a store |
| Daemon glue (Main.cpp, SessionHost.cpp, FluxCLI.cpp in full) | CI ndk-build only (not monitored) |
| Forbidden-symbol check | clean |
| CI | triggered, not monitored (owner instruction) |
| Device | NOT_TESTED |

## Step 8.12 — Observatory historical analysis (`0c23fc0`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without the analyzer sources (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 29/29 (new `observatory_analysis_test`: 12 test functions covering the 20 required cases) |
| clang-18 -Werror syntax (timeline, explanation, analyzer, CLI, store, test) | PASS |
| FluxCLI `telemetry_handler` delegate compiled standalone | PASS (full FluxCLI.cpp: CI ndk-build only — spdlog absent locally) |
| Read-only: file sizes + mtimes unchanged after analyze/history/CLI; corrupt lines not repaired | PASS |
| Determinism: identical text from a fresh read-only store | PASS |
| Forbidden-symbol check | clean |
| Android CI | triggered, **not checked** (owner instruction) |
| Device | NOT_TESTED |

## Step 8.13 — Decision & policy foundation (`7f84189`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without the policy sources (red) | confirmed |
| Host ctest (gcc, -Werror) | PASS 30/30 (new `policy_decision_test`: 11 functions covering the 20 required cases) |
| clang-18 -Werror (C++20 sources + test; C++2b DecisionEngine) | PASS |
| Policy sources contain no file I/O / writes (grep) | confirmed |
| Event registry unchanged (19 types), schema v1 (test) | PASS |
| Forbidden-symbol check | clean |
| Android CI | triggered, **not checked** |
| Device | NOT_TESTED (no call path) |

Open device blockers unchanged: B-26, B-33A, B-33B, B-33C, B-34C, B-36; B-35 deferred (owner); new B-37.

## Step 8.14 — Capability verification foundation (`181af15`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `CapabilityVerification.cpp` (red) | confirmed |
| Host ctest (gcc, -Wall -Wextra -Wpedantic -Werror) | PASS 31/31 (new `capability_verification_test`: 5 functions covering the 24 required cases) |
| clang-18 -Werror (C++20 + C++2b) | PASS |
| `CapabilityHost.cpp` (fluxd glue) compiled against stubs (logging/properties) | PASS; full fluxd only via CI ndk-build |
| Forbidden-symbol check | clean |
| Android CI | triggered, **not checked** |
| Device | NOT_TESTED — verified=true never observed on a real device |

Open: B-26, B-33A, B-33B, B-33C, B-34C, B-36, B-37 (host only); B-35 deferred.

## Phase 4B — Controlled policy executor (`eb329aa`)

| Check | Result |
|---|---|
| Tests first; CMake configure failed without `PolicyExecutor.cpp` (red) | confirmed |
| Host ctest (gcc, -Wall -Wextra -Wpedantic -Werror) | PASS 32/32 (new `policy_executor_test`: 6 functions covering the 33 required cases) |
| clang-18 -Werror (C++20 + C++2b) | PASS |
| Write-path scan of jni/policy: only the counting Io wrapper handed to NodeWriteOperation; no system/popen/exec/fopen | confirmed |
| Forbidden-symbol check | clean |
| Android CI | triggered, **not checked** |
| Device | NOT_TESTED (no call path) |

Open: B-26, B-33A, B-33B, B-33C, B-34C, B-36, B-37 (host only), B-38; B-35 deferred.

## Phase 4C — Live decision integration (`227d90d`)

- Host:
  - `ctest` 33/33 PASS (C++20, `-Werror`).
  - New `live_policy_test` (7 functions) covers the 26 listed cases:
    - B-38 live;
    - stale thermal;
    - fresh evidence, no repeats and final result untouched;
    - gating (session, recovery, conflict);
    - restore before GameRuntime for all end reasons and for switch;
    - executor, verify and restore failures;
    - isolation (no paths, shell, threads, collectors or transaction framework) and schema v1/19.
  - `policy_decision_test` updated for B-38, adding `test_b38_mitigate_semantics`.
- `jni/SessionHost.cpp`: stub syntax check passed (C++23, `-Werror`). `LivePolicyController.cpp`: C++23 syntax check passed.
- Android CI (ndk-build): triggered by the push, **not checked**.
- Device: **NOT_TESTED**.

## Phase 4.5 — Brand & namespace migration (`5f6135a`)

- Host `ctest` 35/35 PASS (`-Wall -Wextra -Wpedantic -Werror`). New tests:
  - `brand_migration_test` (4 functions covering the 24 listed cases);
  - `brand_inventory_test.sh`.
- `FluxCLI.cpp` passes a C++23 stub syntax check. `customize.sh`, `uninstall.sh` and `action.sh` pass `sh -n`.
- The full fluxd NDK build was not run locally (the spdlog submodule is absent). Android CI was triggered by the push and **not checked**.
- Device: **NOT_TESTED**. In particular, neither the `zairenkai` symlink on Magisk/KSU/APatch nor the module upgrade over an existing install has been tested.
