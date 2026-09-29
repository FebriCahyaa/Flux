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
