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
