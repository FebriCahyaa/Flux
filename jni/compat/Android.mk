LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := FluxCompat

LOCAL_SRC_FILES := $(wildcard $(LOCAL_PATH)/*.cpp)
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES:$(LOCAL_PATH)/%=%)

LOCAL_C_INCLUDES := $(ROOT_PATH)/include

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)

# Pure logic: no device I/O of its own (see Runtime.hpp), so only the model and
# rapidjson are needed.
LOCAL_STATIC_LIBRARIES := rapidjson FluxGfx

LOCAL_CPPFLAGS += -fexceptions -std=c++23 $(FLUX_PERF_FLAGS)
LOCAL_CPPFLAGS += -Wpedantic -Wall -Wextra -Werror -Wformat -Wuninitialized

include $(BUILD_STATIC_LIBRARY)
