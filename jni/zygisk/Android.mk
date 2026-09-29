LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(call my-dir)/..

# Flux Zygisk compatibility provider. Output: libs/<abi>/libflux_zygisk.so, which the packaging
# step installs as zygisk/<abi>.so in the module.
#
# Built against the public Zygisk module API only (include/zygisk.hpp, vendored unmodified; it is
# a system include so its style is not held to our -Werror). c++_static comes from Application.mk
# and symbols are hidden, so nothing of the STL leaks into a game process.
include $(CLEAR_VARS)
LOCAL_MODULE := flux_zygisk

LOCAL_SRC_FILES := FluxCompatModule.cpp GotHook.cpp Interpose.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(ROOT_PATH)/compat $(ROOT_PATH)/include

# Only the objects the provider references are pulled from the compat archive
# (plan contract, identity validation, vocabulary); the resolver and daemon-side pieces stay out.
LOCAL_STATIC_LIBRARIES := rapidjson FluxCompat

LOCAL_CPPFLAGS += -fexceptions -std=c++23 $(FLUX_PERF_FLAGS)
LOCAL_CPPFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
LOCAL_CPPFLAGS += -isystem $(LOCAL_PATH)/include
LOCAL_CPPFLAGS += -Wpedantic -Wall -Wextra -Werror -Wformat -Wuninitialized

LOCAL_LDFLAGS += $(FLUX_LINK_FLAGS)

include $(BUILD_SHARED_LIBRARY)
