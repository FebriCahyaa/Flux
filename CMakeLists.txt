# Host-side test harness for Flux.
#
# The shipped fluxd binary is built by ndk-build (jni/Android.mk) and nothing here
# changes that: this project exists only so the parts of the daemon that carry real
# logic can be compiled and exercised on the build machine, with no device and no
# NDK. Device paths are never touched — each test drives its subject through the
# injection seam that subject already has (RefreshHold::Io, SystemQuery, a fake
# sysfs root), so a host run cannot read or write the real /sys, /proc or /data.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build -j"$(nproc)"
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(flux_host_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The device build is C++23; the host tests only need C++20, which every CI
# compiler has. Sources shared with the daemon must stay inside that subset.

set(FLUX_WARNINGS -Wall -Wextra -Wpedantic -Werror -Wformat -Wuninitialized)

find_package(Threads REQUIRED)

set(RAPIDJSON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/jni/external/rapidjson/include")
if(NOT EXISTS "${RAPIDJSON_INCLUDE}/rapidjson/document.h")
  message(FATAL_ERROR
    "rapidjson headers are missing. This is a git submodule:\n"
    "  git submodule update --init jni/external/rapidjson")
endif()

# ---------------------------------------------------------------------------
# flux_gfx — the capability layer, built for the host exactly as it is for the
# device. Vulkan is reached through dlopen, so this links without any Vulkan SDK
# and the probe simply reports the loader as absent when the machine has none.
# ---------------------------------------------------------------------------
add_library(flux_gfx STATIC
  jni/gfx/CapabilityModel.cpp
  jni/gfx/CapabilityCollector.cpp
  jni/gfx/VulkanProbe.cpp
  jni/gfx/PlatformProbe.cpp
)
target_include_directories(flux_gfx PUBLIC jni/gfx jni/include)
target_include_directories(flux_gfx SYSTEM PUBLIC "${RAPIDJSON_INCLUDE}")
target_compile_options(flux_gfx PRIVATE ${FLUX_WARNINGS})
target_link_libraries(flux_gfx PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)

enable_testing()

# Each test is a standalone executable with its own main() and CHECK macro,
# matching the two tests that predate this harness.
function(flux_add_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE jni jni/include)
  target_compile_options(${name} PRIVATE ${FLUX_WARNINGS})
  target_link_libraries(${name} PRIVATE Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# -- tests that predate this harness ----------------------------------------
# Both compile a daemon source directly with a host guard that stubs out the
# device I/O; keep the guard defines in step with the comment at the top of each.
flux_add_test(refresh_hold_test tests/refresh_hold_test.cpp jni/RefreshHold.cpp)
target_compile_definitions(refresh_hold_test PRIVATE FLUX_REFRESH_HOST_TEST)

flux_add_test(render_booster_test tests/render_booster_test.cpp jni/RenderBooster.cpp)
target_compile_definitions(render_booster_test PRIVATE FLUX_RENDER_HOST_TEST)

# -- capability layer --------------------------------------------------------
flux_add_test(capability_model_test tests/capability_model_test.cpp)
target_link_libraries(capability_model_test PRIVATE flux_gfx)

flux_add_test(capability_schema_test tests/capability_schema_test.cpp)
target_link_libraries(capability_schema_test PRIVATE flux_gfx)
target_compile_definitions(capability_schema_test
  PRIVATE FLUX_SCHEMA_JSON="${CMAKE_CURRENT_SOURCE_DIR}/jni/gfx/capability_schema_v4.json")

flux_add_test(vulkan_probe_test tests/vulkan_probe_test.cpp)
target_link_libraries(vulkan_probe_test PRIVATE flux_gfx)

flux_add_test(platform_probe_test tests/platform_probe_test.cpp)
target_link_libraries(platform_probe_test PRIVATE flux_gfx)

# The v3 status-file parser is shared with the daemon; this proves a v4 producer
# does not break it.
flux_add_test(synthesis_core_test tests/synthesis_core_test.cpp jni/base/SynthesisCore/SynthesisCore.cpp)
target_include_directories(synthesis_core_test PRIVATE jni/base/SynthesisCore)

# -- runtime transaction engine ------------------------------------------------
# Host-only for now: jni/Android.mk builds jni/*.cpp, so fluxd does not link this
# until the Game Runtime integration wires it in (GAME_RUNTIME_MIGRATION_PLAN.md).
add_library(flux_runtime STATIC jni/runtime/Transaction.cpp)
target_include_directories(flux_runtime PUBLIC jni/runtime)
target_compile_options(flux_runtime PRIVATE ${FLUX_WARNINGS})

flux_add_test(transaction_test tests/transaction_test.cpp)
target_link_libraries(transaction_test PRIVATE flux_runtime)

# -- shared capability context (no probing, no writes) ---------------------------
add_library(flux_context STATIC jni/context/CapabilityContext.cpp)
target_include_directories(flux_context PUBLIC jni/context)
target_compile_options(flux_context PRIVATE ${FLUX_WARNINGS})

# -- performance planner (on top of the transaction engine) --------------------
add_library(flux_perf STATIC jni/perf/PerformancePlanner.cpp jni/perf/ProfileModel.cpp
  jni/perf/GamePerformanceRuntime.cpp jni/perf/RuntimeHost.cpp)
target_include_directories(flux_perf PUBLIC jni/perf)
target_include_directories(flux_perf SYSTEM PRIVATE "${RAPIDJSON_INCLUDE}")
target_link_libraries(flux_perf PUBLIC flux_runtime flux_context)
target_compile_options(flux_perf PRIVATE ${FLUX_WARNINGS})

flux_add_test(performance_planner_test tests/performance_planner_test.cpp)
target_link_libraries(performance_planner_test PRIVATE flux_perf)

flux_add_test(profile_model_test tests/profile_model_test.cpp)
target_link_libraries(profile_model_test PRIVATE flux_perf)

flux_add_test(game_runtime_test tests/game_runtime_test.cpp)
target_link_libraries(game_runtime_test PRIVATE flux_perf)

flux_add_test(runtime_host_test tests/runtime_host_test.cpp)
target_link_libraries(runtime_host_test PRIVATE flux_perf)

# Shell test of the game-time refresh writer (flux_refresh) against fake dumpsys/settings.
add_test(NAME refresh_script_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/refresh_script_test.sh
         ${CMAKE_CURRENT_SOURCE_DIR}/scripts/flux_profiler.sh)

# -- session lifecycle ----------------------------------------------------------
add_library(flux_session STATIC jni/session/SessionManager.cpp)
target_include_directories(flux_session PUBLIC jni/session)
target_compile_options(flux_session PRIVATE ${FLUX_WARNINGS})

flux_add_test(session_manager_test tests/session_manager_test.cpp)
target_link_libraries(session_manager_test PRIVATE flux_session)

# -- observatory event model (additive; no producers wired yet) ------------------
add_library(flux_observatory STATIC jni/observatory/Event.cpp jni/observatory/EventStore.cpp)
target_include_directories(flux_observatory PUBLIC jni/observatory)
target_include_directories(flux_observatory SYSTEM PRIVATE "${RAPIDJSON_INCLUDE}")
target_compile_options(flux_observatory PRIVATE ${FLUX_WARNINGS})

flux_add_test(observatory_test tests/observatory_test.cpp)
target_link_libraries(observatory_test PRIVATE flux_observatory)

# -- observatory integration bridge ----------------------------------------------
add_library(flux_bridge STATIC jni/bridge/ObservatoryBridge.cpp jni/bridge/BottleneckEvents.cpp)
target_include_directories(flux_bridge PUBLIC jni/bridge)
target_link_libraries(flux_bridge PUBLIC flux_observatory flux_perf flux_session flux_bottleneck)
target_compile_options(flux_bridge PRIVATE ${FLUX_WARNINGS})

flux_add_test(observatory_bridge_test tests/observatory_bridge_test.cpp)
target_link_libraries(observatory_bridge_test PRIVATE flux_bridge)

# -- kernel intelligence (observation only; no writes) ---------------------------
add_library(flux_kernel STATIC jni/kernel/KernelIntelligence.cpp jni/kernel/CapabilityBootstrap.cpp)
target_include_directories(flux_kernel PUBLIC jni/kernel)
target_link_libraries(flux_kernel PUBLIC flux_context)
target_compile_options(flux_kernel PRIVATE ${FLUX_WARNINGS})

flux_add_test(kernel_intelligence_test tests/kernel_intelligence_test.cpp)
target_link_libraries(kernel_intelligence_test PRIVATE flux_kernel)

flux_add_test(capability_context_test tests/capability_context_test.cpp)
target_link_libraries(capability_context_test PRIVATE flux_kernel flux_perf)

flux_add_test(capability_bootstrap_test tests/capability_bootstrap_test.cpp)
target_link_libraries(capability_bootstrap_test PRIVATE flux_kernel)

# -- graphics intelligence (capability only; no policy, no Vulkan instance) ------
add_library(flux_graphics STATIC jni/graphics/GraphicsIntelligence.cpp)
target_include_directories(flux_graphics PUBLIC jni/graphics)
target_link_libraries(flux_graphics PUBLIC flux_kernel flux_context flux_gfx)
target_compile_options(flux_graphics PRIVATE ${FLUX_WARNINGS})

flux_add_test(graphics_intelligence_test tests/graphics_intelligence_test.cpp)
target_link_libraries(graphics_intelligence_test PRIVATE flux_graphics)

# -- display & rendering capability (read-only; no policy) -----------------------
add_library(flux_display STATIC jni/display/DisplayIntelligence.cpp)
target_include_directories(flux_display PUBLIC jni/display)
target_link_libraries(flux_display PUBLIC flux_context)
target_compile_options(flux_display PRIVATE ${FLUX_WARNINGS})

flux_add_test(display_intelligence_test tests/display_intelligence_test.cpp)
target_link_libraries(display_intelligence_test PRIVATE flux_display)

# -- bottleneck observation (read-only; no actions) ------------------------------
add_library(flux_bottleneck STATIC jni/bottleneck/BottleneckModel.cpp jni/bottleneck/BottleneckResult.cpp)
target_include_directories(flux_bottleneck PUBLIC jni/bottleneck)
target_link_libraries(flux_bottleneck PUBLIC flux_context)
target_compile_options(flux_bottleneck PRIVATE ${FLUX_WARNINGS})

flux_add_test(bottleneck_model_test tests/bottleneck_model_test.cpp)
target_link_libraries(bottleneck_model_test PRIVATE flux_bottleneck)

# -- thermal context (read-only; Synrei adapter) ---------------------------------
add_library(flux_thermal STATIC jni/thermal/ThermalContext.cpp jni/thermal/SynreiThermalAdapter.cpp)
target_include_directories(flux_thermal PUBLIC jni/thermal)
target_link_libraries(flux_thermal PUBLIC flux_kernel flux_bottleneck flux_context)
target_compile_options(flux_thermal PRIVATE ${FLUX_WARNINGS})

# -- runtime metrics collector (read-only; feeds the bottleneck model) -----------
add_library(flux_metrics STATIC jni/metrics/RuntimeMetrics.cpp jni/metrics/RuntimeMetricsSampler.cpp
  jni/metrics/FpsObservation.cpp)
target_include_directories(flux_metrics PUBLIC jni/metrics)
target_link_libraries(flux_metrics PUBLIC flux_kernel flux_bottleneck flux_context flux_session flux_thermal)
target_compile_options(flux_metrics PRIVATE ${FLUX_WARNINGS})

flux_add_test(runtime_metrics_test tests/runtime_metrics_test.cpp)
target_link_libraries(runtime_metrics_test PRIVATE flux_metrics)

flux_add_test(runtime_metrics_sampler_test tests/runtime_metrics_sampler_test.cpp)
target_link_libraries(runtime_metrics_sampler_test PRIVATE flux_metrics)

flux_add_test(fps_observation_test tests/fps_observation_test.cpp)
target_link_libraries(fps_observation_test PRIVATE flux_metrics)

flux_add_test(synrei_thermal_context_test tests/synrei_thermal_context_test.cpp)
target_link_libraries(synrei_thermal_context_test PRIVATE flux_metrics flux_thermal)

flux_add_test(bottleneck_result_test tests/bottleneck_result_test.cpp)
target_link_libraries(bottleneck_result_test PRIVATE flux_metrics flux_bridge)
