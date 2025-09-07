LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libdobby
LOCAL_SRC_FILES := libdobby.a
include $(PREBUILT_STATIC_LIBRARY)



include $(CLEAR_VARS)

LOCAL_MODULE := libaudiohook
LOCAL_SRC_FILES := \
    audioserver_hook_dobby.cpp \
    get_lib_address.c \

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \

LOCAL_CFLAGS := -Wall -O2 -fPIC
LOCAL_CPPFLAGS := -std=c++11 -fno-rtti -fno-exceptions

LOCAL_LDLIBS := -llog -ldl



LOCAL_STATIC_LIBRARIES := libdobby  

include $(BUILD_SHARED_LIBRARY)
