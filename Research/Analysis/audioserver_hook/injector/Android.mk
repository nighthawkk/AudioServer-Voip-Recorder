# Android.mk
# Build configuration for C++ Android Injector

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := injector
LOCAL_SRC_FILES := \
    main.cpp \
    injector.cpp \
    ptrace_wrapper.cpp \
    process_utils.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)

# Compiler flags
LOCAL_CFLAGS := -Wall -Wextra -O2 -fexceptions
LOCAL_CPPFLAGS := -std=c++14 -DDEBUG

# Link libraries
LOCAL_LDLIBS := -llog

# Static build for better compatibility
# LOCAL_LDFLAGS := -static

# Build as executable
include $(BUILD_EXECUTABLE)