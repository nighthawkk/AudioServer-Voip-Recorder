#define _GNU_SOURCE
#include <dlfcn.h>
#include <android/log.h>
#include <unistd.h>
#include "plthook.h"

#define LOG_TAG "AF-POC"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

typedef int (*threadLoop_write_t)(void *thiz);
static threadLoop_write_t orig_threadLoop_write = NULL;

typedef void *(*createRecordTrack_l_t)(void *thiz, void *client,
    const void *attr, unsigned int *pId, int format, unsigned int channelMask,
    unsigned int *flags, uid_t uid, const void *srcState,
    unsigned int *outFrameCount, int sessionId, int *portId, int portHandle, int something);
static createRecordTrack_l_t orig_createRecordTrack_l = NULL;

int hook_threadLoop_write(void *thiz) {
    LOGI("Playback thread writing audio (possible VOIP)");
    return orig_threadLoop_write(thiz);
}

void *hook_createRecordTrack_l(void *thiz, void *client, const void *attr,
    unsigned int *pId, int format, unsigned int channelMask,
    unsigned int *flags, uid_t uid, const void *srcState,
    unsigned int *outFrameCount, int sessionId, int *portId, int portHandle, int something) {

    LOGI("New RecordTrack uid=%d (check if VOICE_COMMUNICATION via attr)", uid);
    return orig_createRecordTrack_l(thiz, client, attr, pId, format, channelMask,
                                    flags, uid, srcState, outFrameCount, sessionId,
                                    portId, portHandle, something);
}

__attribute__((constructor))
void init_hook() {
    plthook_t *plthook;
    if (plthook_open(&plthook, "/system/lib64/libaudioflinger.so") != 0) {
        LOGI("plthook_open failed: %s", plthook_error());
        return;
    }

    if (plthook_replace(plthook, "_ZN7android14PlaybackThread16threadLoop_writeEv",
                        (void*)hook_threadLoop_write,
                        (void**)&orig_threadLoop_write) == 0) {
        LOGI("Hooked threadLoop_write");
    }

    if (plthook_replace(plthook, "_ZN7android12RecordThread21createRecordTrack_lERKNS_2spINS_6ClientEEE...",
                        (void*)hook_createRecordTrack_l,
                        (void**)&orig_createRecordTrack_l) == 0) {
        LOGI("Hooked createRecordTrack_l");
    }

    plthook_close(plthook);
}
