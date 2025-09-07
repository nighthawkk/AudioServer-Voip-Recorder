// audioserver_hook_dobby.cpp
// C++ implementation of audioflinger hooks using Dobby (inline hooking)
// Captures RecordTrack (mic) and PlaybackTrack (speaker) audio

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <map>
#include <string>
#include <android/log.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <fstream>
#include <sstream>

#include "../include/get_lib_address.h"

#include "dobby.h" // adjust include path if needed: <dobby/dobby.h>

#define LOG_TAG "AudioHook"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Offsets from your reversed analysis
// android::AudioFlinger::ThreadBase::TrackBase::TrackBase(android::AudioFlinger::ThreadBase*,android::sp<android::AudioFlinger::Client> const&,audio_attributes_t const&,uint,audio_format_t,audio_channel_mask_t,ulong,void *,ulong,audio_session_t,int,uint,bool,android::AudioFlinger::ThreadBase::TrackBase::alloc_type,android::AudioFlinger::ThreadBase::TrackBase::track_type,int,std::string)
constexpr size_t SAMPLERATE_OFFSET = 0x178;
constexpr size_t UID_OFFSET = 0x234;
constexpr size_t ISOUT_OFFSET = 0x1C8;
constexpr size_t FORMAT_OFFSET = 0x17C;
constexpr size_t CHANNELMASK_OFFSET = 0x180;
constexpr size_t ATTR_OFFSET = 0x68;

// Audio format enum values from AOSP
enum AudioFormat {
    AUDIO_FORMAT_PCM_16_BIT = 0x1,
    AUDIO_FORMAT_PCM_32_BIT = 0x2,
    AUDIO_FORMAT_PCM_FLOAT = 0x3,
    AUDIO_FORMAT_PCM_8_BIT = 0x4,
    AUDIO_FORMAT_PCM_24_BIT_PACKED = 0x5
};

// Audio source/usage enums
enum {
    AUDIO_SOURCE_VOICE_COMMUNICATION = 7,
    AUDIO_USAGE_VOICE_COMMUNICATION = 2
};

// AudioBufferProvider::Buffer structure
struct AudioBuffer {
    void* raw;           // offset 0
    size_t frameCount;   // offset 8
};

// Audio attributes structure
struct AudioAttributes {
    int32_t contentType;
    int32_t usage;
    int32_t source;
    uint32_t flags;
};

// Track info for file management
struct TrackInfo {
    FILE* file;
    std::string tmpPath;
    std::string kind;
    uint32_t uid;
    uint32_t sampleRate;
    bool isFirstMicStop = true;
};

// Global state
static std::map<void*, TrackInfo> g_tracks;
static std::string g_sessionDir = [](){
    char buf[256];
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    snprintf(buf, sizeof(buf), "/data/misc/audioserver/audioserver_session_%ld", tv.tv_sec);
    mkdir(buf, 0755);
    LOGD("Session directory: %s", buf);
    return std::string(buf);
}();

static const char* kTargetPackages[] = {
    "com.whatsapp",
    "com.whatsapp.w4b",
    "org.thoughtcrime.securesms",
    "org.telegram.messenger",
    "org.telegram.messenger.web"
};

static std::unordered_map<uid_t, std::string> gUidToPackage;

static void refreshPackageCache() {
    FILE* fp = popen("cmd package list packages -U", "r");
    if (!fp) {
        LOGD("Failed to run cmd package");
        return;
    }

    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        // Format: package:com.whatsapp uid:10123
        std::string line(buf);
        auto pkgPos = line.find("package:");
        auto uidPos = line.find(" uid:");
        if (pkgPos == std::string::npos || uidPos == std::string::npos) continue;

        std::string packageName = line.substr(pkgPos + 8, uidPos - (pkgPos + 8));
        uid_t uid = static_cast<uid_t>(std::stoi(line.substr(uidPos + 5)));

        gUidToPackage[uid] = packageName;
    }
    pclose(fp);
}


// Audio mode constants
#define AUDIO_MODE_NORMAL 0
#define AUDIO_MODE_RINGTONE 1
#define AUDIO_MODE_IN_CALL 2
#define AUDIO_MODE_IN_COMMUNICATION 3
#define AUDIO_MODE_CALL_SCREEN 4

// Global mode tracking
static int g_currentAudioMode = AUDIO_MODE_NORMAL;
static bool g_modeHookInstalled = false;

const char* getModeString(int mode) {
    switch (mode) {
        case AUDIO_MODE_NORMAL: return "NORMAL";
        case AUDIO_MODE_RINGTONE: return "RINGTONE";
        case AUDIO_MODE_IN_CALL: return "IN_CALL";
        case AUDIO_MODE_IN_COMMUNICATION: return "IN_COMMUNICATION";
        case AUDIO_MODE_CALL_SCREEN: return "CALL_SCREEN";
        default: return "UNKNOWN";
    }
}

// Original function pointers
static void (*orig_AudioFlinger_setMode)(void* thisPtr, int mode);

static int (*orig_RecordTrack_getNextBuffer)(void* thisPtr, AudioBuffer* buffer) = nullptr;
static int (*orig_Track_getNextBuffer)(void* thisPtr, AudioBuffer* buffer) = nullptr;
static void (*orig_Track_stop)(void* thisPtr) = nullptr;
static void (*orig_RecordTrack_stop)(void* thisPtr) = nullptr;

// Hook AudioFlinger::setMode to track mode changes
static void hook_AudioFlinger_setMode(void* thisPtr, int mode) {
    LOGD("🔄 AudioFlinger::setMode called: %s (%d) -> %s (%d)",
         getModeString(g_currentAudioMode), g_currentAudioMode,
         getModeString(mode), mode);
    
    g_currentAudioMode = mode;
    
    if (mode == AUDIO_MODE_IN_COMMUNICATION) {
        LOGD("📞 COMMUNICATION MODE ACTIVATED - Audio capture will start");
    } else if (g_currentAudioMode == AUDIO_MODE_IN_COMMUNICATION) {
        LOGD("📴 COMMUNICATION MODE DEACTIVATED - Audio capture will stop");
    }
    
    // Call original function
    orig_AudioFlinger_setMode(thisPtr, mode);
}

// Simple mode checking function
bool isInCommunicationMode() {
    bool is_comm = (g_currentAudioMode == AUDIO_MODE_IN_COMMUNICATION);
    
    if (!g_modeHookInstalled) {
        LOGD("⚠️ Mode hook not installed - cannot verify communication mode");
        return false;
    }
    
    return is_comm;
}

// Helper functions (unchanged)
static int popcount(uint32_t x) {
    int count = 0;
    while (x) {
        x &= x - 1;
        count++;
    }
    return count;
}

static int audioChannelCountFromOutMask(uint32_t channel) {
    const uint32_t AUDIO_CHANNEL_OUT_ALL = 0x3FFFF;
    return popcount(channel & AUDIO_CHANNEL_OUT_ALL);
}

static int audioChannelCountFromInMask(uint32_t channel) {
    const uint32_t AUDIO_CHANNEL_IN_ALL = 0xFFFC;
    return popcount(channel & AUDIO_CHANNEL_IN_ALL);
}

static int bytesPerSample(uint32_t format) {
    switch (format) {
        case AUDIO_FORMAT_PCM_16_BIT: return 2;
        case AUDIO_FORMAT_PCM_32_BIT: return 4;
        case AUDIO_FORMAT_PCM_FLOAT: return 4;
        case AUDIO_FORMAT_PCM_8_BIT: return 1;
        case AUDIO_FORMAT_PCM_24_BIT_PACKED: return 3;
        default: return 2;
    }
}

static AudioAttributes* readAttributes(void* thisPtr) {
    return reinterpret_cast<AudioAttributes*>(
        reinterpret_cast<uint8_t*>(thisPtr) + ATTR_OFFSET);
}

static uint32_t readUid(void* thisPtr) {
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(thisPtr) + UID_OFFSET);
}

static uint32_t readSampleRate(void* thisPtr) {
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(thisPtr) + SAMPLERATE_OFFSET);
}

static size_t calculateFrameSize(void* thisPtr) {
    uint32_t isOut = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(thisPtr) + ISOUT_OFFSET);
    
    uint32_t channelMask = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(thisPtr) + CHANNELMASK_OFFSET);
    
    uint32_t format = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(thisPtr) + FORMAT_OFFSET);
    
    int channelCount;
    if (isOut == 1) {
        channelCount = audioChannelCountFromOutMask(channelMask);
    } else {
        channelCount = audioChannelCountFromInMask(channelMask);
    }
    
    size_t frameSize = channelCount * bytesPerSample(format);
    if (isOut == 0) {
        frameSize = 1; // Fallback for mic
    }
    
    LOGD("isOut=%u, format=0x%x, channels=%d, frameSize=%zu", 
         isOut, format, channelCount, frameSize);
    
    return frameSize;
}

static std::string generateFileName(const char* pkg, uint32_t sampleRate) {
    char buf[128];
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    snprintf(buf, sizeof(buf), "%s_%d_%ld_%06ld", pkg, sampleRate, tv.tv_sec, tv.tv_usec);
    return std::string(buf);
}

static FILE* openTrackFile(void* thisPtr, uint32_t uid, const char* pkg, uint32_t sampleRate, const char* kind) {
    if (g_tracks.find(thisPtr) != g_tracks.end()) {
        return g_tracks[thisPtr].file;
    }
    
    std::string filename = g_sessionDir + "/" + generateFileName(pkg, sampleRate) + ".tmp";
    FILE* file = fopen(filename.c_str(), "ab");
    if (!file) {
        LOGE("Failed to open file: %s", filename.c_str());
        return nullptr;
    }
    
    TrackInfo info;
    info.file = file;
    info.tmpPath = filename;
    info.kind = kind;
    info.uid = uid;
    info.sampleRate = sampleRate;
    
    g_tracks[thisPtr] = info;
    LOGD("Opened %s for %s (uid=%u, rate=%u)", 
         filename.c_str(), kind, info.uid, info.sampleRate);
    
    return file;
}

static void closeTrackFile(void* thisPtr, const char* ext) {
    auto it = g_tracks.find(thisPtr);
    if (it == g_tracks.end()) return;
    
    TrackInfo& info = it->second;

    if (info.isFirstMicStop && ext == ".bc"){
        info.isFirstMicStop = false;
        return;
    }
    if (info.file) {
        fclose(info.file);
        
        // Rename from .tmp to final extension
        std::string finalPath = info.tmpPath.substr(0, info.tmpPath.rfind(".tmp")) + ext;
        rename(info.tmpPath.c_str(), finalPath.c_str());
        
        LOGD("Closed and renamed %s -> %s", info.tmpPath.c_str(), finalPath.c_str());
    }
    
    g_tracks.erase(it);
}

static const char* getTargetPackage(uint32_t uid) {
    auto it = gUidToPackage.find(uid);
    if (it == gUidToPackage.end()) {
        refreshPackageCache();
        it = gUidToPackage.find(uid);
        if (it == gUidToPackage.end()) {
            LOGD("UID %u not found in package cache", uid);
            return nullptr;
        }
    }

    const std::string& pkg = it->second;

    // Check against target apps
    for (auto target : kTargetPackages) {
        if (pkg == target) {
            LOGD("UID %u → %s [MATCH], recording", uid, pkg.c_str());
            return pkg.c_str();
        }
    }

    LOGD("UID %u → %s [NOT TARGET], skipping", uid, pkg.c_str());
    return nullptr;
}

// Hook implementations (unchanged)
static int hook_RecordTrack_getNextBuffer(void* thisPtr, AudioBuffer* buffer) {
    LOGD("RecordTrack::getNextBuffer called");
    int ret = orig_RecordTrack_getNextBuffer ? orig_RecordTrack_getNextBuffer(thisPtr, buffer) : -1;
    
    if (ret < 0 || !buffer || !buffer->raw) return ret;

    if (!isInCommunicationMode()) {
        return ret; // Skip if not in communication mode
    }
    
    AudioAttributes* attr = readAttributes(thisPtr);
    if (!attr || attr->source != AUDIO_SOURCE_VOICE_COMMUNICATION) {
        return ret;
    }
    LOGD("RecordTrack attr->source %d", attr->source);
    
    uint32_t uid = readUid(thisPtr);
    uint32_t sampleRate = readSampleRate(thisPtr);

    const char* pkg = getTargetPackage(uid);
    if (!pkg) {
        // Only target apps reach here
        LOGD("UID %u is not a target app, skipping", uid);
        return ret;
    }
    if (std::strstr(pkg, "org.telegram.messenger") != nullptr && sampleRate != 48000) {
        return ret;
    }
    if (std::strstr(pkg, "org.thoughtcrime.securesms") != nullptr && sampleRate != 8000) {
        return ret;
    }

    // size_t frameSize = calculateFrameSize(thisPtr);
    size_t bufferSize = buffer->frameCount * 0.5;
    
    FILE* file = openTrackFile(thisPtr, uid, pkg, sampleRate, "uplink");
    if (file && bufferSize > 0) {
        size_t written = fwrite(buffer->raw, 1, bufferSize, file);
        LOGD("[MIC] Written %zu bytes (frames=%zu, frameSize=%zu)", 
             written, buffer->frameCount, 0.5);
        fflush(file);
    }
    
    return ret;
}

static int hook_Track_getNextBuffer(void* thisPtr, AudioBuffer* buffer) {
    LOGD("Track::getNextBuffer called");
    int ret = orig_Track_getNextBuffer ? orig_Track_getNextBuffer(thisPtr, buffer) : -1;
    
    if (ret < 0 || !buffer || !buffer->raw) return ret;
    
    AudioAttributes* attr = readAttributes(thisPtr);

    if (!isInCommunicationMode()) {
        return ret; // Skip if not in communication mode
    }

    if (!attr || attr->usage != AUDIO_USAGE_VOICE_COMMUNICATION) {
        return ret;
    }
    LOGD("Track attr->usage %d", attr->usage);

    uint32_t uid = readUid(thisPtr);
    uint32_t sampleRate = readSampleRate(thisPtr);

    const char* pkg = getTargetPackage(uid);

    if (!pkg) {
        // Only target apps reach here
        LOGD("UID %u is not a target app, skipping", uid);
        return ret;
    }
    if (std::strstr(pkg, "org.telegram.messenger") != nullptr && sampleRate != 48000) {
        return ret;
    }
    if (std::strstr(pkg, "org.thoughtcrime.securesms") != nullptr && sampleRate != 8000) {
        return ret;
    }
    
    size_t frameSize = calculateFrameSize(thisPtr);
    size_t bufferSize = buffer->frameCount * frameSize;
    
    FILE* file = openTrackFile(thisPtr, uid, pkg, sampleRate, "downlink");
    if (file && bufferSize > 0) {
        size_t written = fwrite(buffer->raw, 1, bufferSize, file);
        LOGD("[SPEAKER] Written %zu bytes (frames=%zu, frameSize=%zu)", 
             written, buffer->frameCount, frameSize);
        fflush(file);
    }
    
    return ret;
}

static void hook_Track_stop(void* thisPtr) {
    LOGD("Track::stop called");
    closeTrackFile(thisPtr, ".ac");
    if (orig_Track_stop) orig_Track_stop(thisPtr);
}

static void hook_RecordTrack_stop(void* thisPtr) {
    LOGD("RecordTrack::stop called");
    closeTrackFile(thisPtr, ".bc");
    if (orig_RecordTrack_stop) orig_RecordTrack_stop(thisPtr);
}

#define AUDIOFLINGER_SETMODE_OFFSET 0x4DE40
// android::AudioFlinger::setMode(audio_mode_t)	000000000004DE40	
#define RECORDTRACK_GETNEXTBUFFER_OFFSET 0x1364F0
// android::AudioFlinger::RecordThread::RecordTrack::getNextBuffer(android::AudioBufferProvider::Buffer *)	00000000001364F0	
#define TRACK_GETNEXTBUFFER_OFFSET 0x12C2A0
// android::AudioFlinger::PlaybackThread::Track::getNextBuffer(android::AudioBufferProvider::Buffer *)	000000000012C2A0	
#define TRACK_STOP_OFFSET 0x12D7D0
// android::AudioFlinger::PlaybackThread::Track::stop(void)	000000000012D7D0	
#define RECORDTRACK_STOP_OFFSET 0x1367C0
// android::AudioFlinger::RecordThread::RecordTrack::stop(void)	00000000001367C0	
// Initialization
extern "C" void init_hooks() {
   
    // Symbol names (mangled)
    
    // const char* sym_RecordTrack_getNextBuffer =
    //     "_ZN7android12AudioFlinger12RecordThread11RecordTrack13getNextBufferEPNS_19AudioBufferProvider6BufferE";
    // const char* sym_Track_getNextBuffer =
    //     "_ZN7android12AudioFlinger14PlaybackThread5Track13getNextBufferEPNS_19AudioBufferProvider6BufferE";
    // const char* sym_Track_stop =
    //     "_ZN7android12AudioFlinger14PlaybackThread5Track4stopEv";
    // const char* sym_RecordTrack_stop =
    //     "_ZN7android12AudioFlinger12RecordThread11RecordTrack4stopEv";


    char *flingerLib = "/system/lib64/libaudioflinger.so";



    uint64_t base = GetLibAddress(flingerLib); 
 
    // Resolve and hook each symbol with Dobby
    void* addr;

    addr = (void*)(base + AUDIOFLINGER_SETMODE_OFFSET);
    // Hook AudioFlinger::setMode first (most important)
    if (DobbyHook(addr, (void*)hook_AudioFlinger_setMode, 
                  (void**)&orig_AudioFlinger_setMode) == 0) {
        LOGD("Hooked AudioFlinger::setMode");
        g_modeHookInstalled = true;
    } else {
        LOGE("CRITICAL: Failed to hook AudioFlinger::setMode");
        LOGE("Mode tracking will not work - captures may be incorrect");
    }

    // RecordTrack::getNextBuffer
    addr = (void*)(base + RECORDTRACK_GETNEXTBUFFER_OFFSET);
    if (addr) {
        if (DobbyHook(addr,
                     (void*)hook_RecordTrack_getNextBuffer,
                     (void**)&orig_RecordTrack_getNextBuffer) == 0) {
            LOGD("Dobby hooked RecordTrack::getNextBuffer at %p", addr);
        } else {
            LOGE("DobbyHook failed for RecordTrack::getNextBuffer at %p", addr);
        }
    } else {
        LOGE("Could not resolve RecordTrack::getNextBuffer via dlsym. "
             "If symbol is non-exported, get address (e.g. from Frida) and call DobbyHook(addr, ...).");
    }

    // Track::getNextBuffer
    addr = (void*)(base + TRACK_GETNEXTBUFFER_OFFSET);
    if (addr) {
        if (DobbyHook(addr,
                     (void*)hook_Track_getNextBuffer,
                     (void**)&orig_Track_getNextBuffer) == 0) {
            LOGD("Dobby hooked Track::getNextBuffer at %p", addr);
        } else {
            LOGE("DobbyHook failed for Track::getNextBuffer at %p", addr);
        }
    } else {
        LOGE("Could not resolve Track::getNextBuffer via dlsym.");
    }

    // Track::stop
    addr = (void*)(base + TRACK_STOP_OFFSET);
    if (addr) {
        if (DobbyHook(addr,
                     (void*)hook_Track_stop,
                     (void**)&orig_Track_stop) == 0) {
            LOGD("Dobby hooked Track::stop at %p", addr);
        } else {
            LOGE("DobbyHook failed for Track::stop at %p", addr);
        }
    } else {
        LOGE("Could not resolve Track::stop via dlsym.");
    }

    // RecordTrack::stop
    addr = (void*)(base + RECORDTRACK_STOP_OFFSET);
    if (addr) {
        if (DobbyHook(addr,
                     (void*)hook_RecordTrack_stop,
                     (void**)&orig_RecordTrack_stop) == 0) {
            LOGD("Dobby hooked RecordTrack::stop at %p", addr);
        } else {
            LOGE("DobbyHook failed for RecordTrack::stop at %p", addr);
        }
    } else {
        LOGE("Could not resolve RecordTrack::stop via dlsym.");
    }

    LOGD("Audio hooks initialized (Dobby)");
    LOGD("Initial mode: %s (%d)", getModeString(g_currentAudioMode), g_currentAudioMode);
    LOGD("Mode tracking: %s", g_modeHookInstalled ? "ACTIVE" : "FAILED");
}

// Constructor for automatic initialization
__attribute__((constructor))
static void on_load() {
    LOGD("AudioHook library loaded");
    init_hooks();
}
