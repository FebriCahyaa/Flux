LOCAL_PATH := $(call my-dir)

# ---------------------------------------------------------------------------
# Architecture-tuned flags shared by all modules in this tree.
# NDK sets TARGET_ARCH_ABI before processing any Android.mk.
# ---------------------------------------------------------------------------
ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
  FLUX_ARCH_FLAGS := \
    -march=armv8.2-a+crypto+dotprod+fp16 \
    -mtune=cortex-a55
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
  FLUX_ARCH_FLAGS := \
    -march=armv7-a \
    -mfloat-abi=softfp \
    -mfpu=neon-vfpv4 \
    -mtune=cortex-a53
else
  FLUX_ARCH_FLAGS :=
endif

# Common perf flags: full LTO, dead-section stripping, ABI-specific tuning.
FLUX_PERF_FLAGS := \
  -O3 \
  -flto \
  -fomit-frame-pointer \
  -fno-plt \
  -fdata-sections \
  -ffunction-sections \
  $(FLUX_ARCH_FLAGS)

FLUX_LINK_FLAGS := \
  -flto \
  -Wl,--gc-sections \
  -Wl,--icf=safe \
  -Wl,-O2

include $(CLEAR_VARS)
LOCAL_MODULE := fluxd

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_STATIC_LIBRARIES := rapidjson spdlog SynthesisCore NativeMonitor BinderNDK PIDTracker InotifyWatcher LockFile GameRegistry FluxUtility DeviceInfo FluxGfx

LOCAL_SRC_FILES := $(wildcard $(LOCAL_PATH)/*.cpp)
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES:$(LOCAL_PATH)/%=%)

LOCAL_CPPFLAGS += -fexceptions -std=c++23 $(FLUX_PERF_FLAGS)
LOCAL_CPPFLAGS += -Wpedantic -Wall -Wextra -Werror -Wformat -Wuninitialized

LOCAL_LDFLAGS += $(FLUX_LINK_FLAGS)

include $(BUILD_EXECUTABLE)

include $(LOCAL_PATH)/external/Android.mk $(LOCAL_PATH)/base/Android.mk $(LOCAL_PATH)/gfx/Android.mk
