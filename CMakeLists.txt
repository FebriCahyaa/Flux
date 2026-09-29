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
