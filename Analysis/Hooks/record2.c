#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <android/log.h>
#include "plthook.h"

#define LOGTAG "AF-PROXY"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOGTAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOGTAG, __VA_ARGS__)

// Basic Buffer struct (match the one in AudioFlinger / your client code)
struct Buffer {
    size_t frameCount;
    size_t size; // bytes
    union {
        void* raw;
        int16_t* i16;
        int8_t* i8;
    };
};

// Simple mapping of object pointer -> fd
#define MAX_ENT 64
static void* keys[MAX_ENT] = {0};
static int   fds[MAX_ENT] = {0};
static char  filenames[MAX_ENT][256];

static int add_entry(void* key, const char* fname) {
    for (int i=0;i<MAX_ENT;i++) {
        if (keys[i] == NULL) {
            keys[i] = key;
            fds[i] = open(fname, O_CREAT|O_WRONLY|O_APPEND, 0644);
            if (fds[i] < 0) {
                ALOGE("open failed %s", fname);
                keys[i] = NULL;
                return -1;
            }
            strncpy(filenames[i], fname, sizeof(filenames[i])-1);
            return i;
        }
    }
    return -1;
}

static int get_fd(void* key) {
    for (int i=0;i<MAX_ENT;i++) {
        if (keys[i] == key) return fds[i];
    }
    return -1;
}

static void remove_entry(void* key) {
    for (int i=0;i<MAX_ENT;i++) {
        if (keys[i] == key) {
            if (fds[i] > 0) close(fds[i]);
            keys[i] = NULL;
            fds[i] = 0;
            filenames[i][0] = 0;
            return;
        }
    }
}

// create a path under /data/misc/audioserver/ using pointer hex + ts
static void make_path_for_key(void* key, char *out, size_t outlen) {
    // ensure dir exists
    mkdir("/data/misc/audioserver", 0750);
    snprintf(out, outlen, "/data/misc/audioserver/track_%p.bin", key);
}

// ------------------------------------------------------------------
// Forward declarations for original functions we will replace via plthook
typedef int (*fn_obtainBuffer_t)(void* thisptr, void* buffer, int someFlag);
typedef int (*fn_releaseBuffer_t)(void* thisptr, void* buffer);

// store originals
static fn_obtainBuffer_t  real_StaticProxy_obtain = NULL;
static fn_releaseBuffer_t real_StaticProxy_release = NULL;

// Hooked obtainBuffer: called when server proxy obtains a buffer from client.
// Signature details may vary; adjust param types to match your nm output.
int hooked_StaticAudioTrackServerProxy_obtainBuffer(void* thisptr, void* proxyBuffer, int forRead) {
    // proxyBuffer is likely pointer to Proxy::Buffer (same layout as Buffer)
    struct Buffer* b = (struct Buffer*)proxyBuffer;
    if (b && b->size > 0 && b->raw) {
        int fd = get_fd(thisptr);
        if (fd < 0) {
            char path[256];
            make_path_for_key(thisptr, path, sizeof(path));
            add_entry(thisptr, path);
            fd = get_fd(thisptr);
            ALOGI("Opened capture file %s for proxy %p", path, thisptr);
        }
        if (fd >= 0) {
            // best-effort non-blocking write
            ssize_t w = write(fd, b->raw, b->size);
            (void)w;
        }
    }
    // call real
    if (real_StaticProxy_obtain) return real_StaticProxy_obtain(thisptr, proxyBuffer, forRead);
    return 0;
}

// Hooked releaseBuffer: called when buffer released; we can close file if desired
int hooked_StaticAudioTrackServerProxy_releaseBuffer(void* thisptr, void* proxyBuffer) {
    // optionally close file when release called (or when stop observed)
    // But releaseBuffer happens frequently; be cautious. Use other lifecycle hooks to finalize.
    // For now just forward to real.
    if (real_StaticProxy_release) return real_StaticProxy_release(thisptr, proxyBuffer);
    return 0;
}

// ------------------------------------------------------------------
// helper to install PLT replacements (call once from constructor)
static void install_hooks() {
    plthook_t *plthook = NULL;

    // open the shared object that exports the symbols
    // try common locations; adjust for your device (apex or system)
    const char *candidates[] = {
        "/system/lib64/libaudioflinger.so",
        "/system/lib64/libaudioclient.so",
        NULL
    };

    for (int i=0; candidates[i]; ++i) {
        if (plthook_open(&plthook, candidates[i]) == 0) {
            ALOGI("plthook_open success on %s", candidates[i]);
            // try replacing obtainBuffer
            if (plthook_replace(plthook,
                "_ZN7android27StaticAudioTrackServerProxy12obtainBufferEPNS_5Proxy6BufferEb",
                (void*)hooked_StaticAudioTrackServerProxy_obtainBuffer,
                (void**)&real_StaticProxy_obtain) == 0) {
                ALOGI("Replaced StaticAudioTrackServerProxy::obtainBuffer");
            } else {
                ALOGI("plthook_replace(obtain) failed: %s", plthook_error());
            }

            if (plthook_replace(plthook,
                "_ZN7android27StaticAudioTrackServerProxy13releaseBufferEPNS_5Proxy6BufferE",
                (void*)hooked_StaticAudioTrackServerProxy_releaseBuffer,
                (void**)&real_StaticProxy_release) == 0) {
                ALOGI("Replaced StaticAudioTrackServerProxy::releaseBuffer");
            } else {
                ALOGI("plthook_replace(release) failed: %s", plthook_error());
            }

            // Also try ServerProxy variants
            if (plthook_replace(plthook,
                "_ZN7android11ServerProxy12obtainBufferEPNS_5Proxy6BufferEb",
                (void*)hooked_StaticAudioTrackServerProxy_obtainBuffer,
                (void**)&real_StaticProxy_obtain) == 0) {
                ALOGI("Replaced ServerProxy::obtainBuffer");
            }

            if (plthook_replace(plthook,
                "_ZN7android27StaticAudioTrackServerProxy13releaseBufferEPNS_5Proxy6BufferE",
                (void*)hooked_StaticAudioTrackServerProxy_releaseBuffer,
                (void**)&real_StaticProxy_release) == 0) {
                ALOGI("Replaced StaticAudioTrackServerProxy::releaseBuffer (dup)");
            }

            plthook_close(plthook);
            return;
        } else {
            ALOGI("plthook_open failed on %s : %s", candidates[i], plthook_error());
        }
    }
    ALOGE("No candidate library opened for plthook.");
}

// ----------------------------------------------------------------------------
__attribute__((constructor))
static void init() {
    ALOGI("AF proxy hook init");
    install_hooks();
}
