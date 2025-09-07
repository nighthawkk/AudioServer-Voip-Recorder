stop audioserver && start audioserver
/data/local/tmp/AndKittyInjector -pkg /system/bin/audioserver -lib /data/local/tmp/libaudiohook.so


# AudioServer Hook - C++ Implementation

Native Android audio interception using PLT hooking in audioflinger. Captures voice communication audio streams (WhatsApp, phone calls, etc.) directly from the audio subsystem.

## Features

- **Dual-channel capture**: Records both uplink (microphone) and downlink (speaker) audio
- **Smart filtering**: Only captures voice communication streams (filters by audio attributes)
- **Zero-copy**: Direct buffer access without memory allocation
- **Automatic file management**: Creates timestamped sessions with proper cleanup
- **Production-ready**: Minimal overhead, thread-safe implementation

## Architecture

```
audioserver (system process)
    ├── libaudioflinger.so
    │   ├── RecordTrack::getNextBuffer() → [HOOK] → Capture mic audio
    │   ├── Track::getNextBuffer()      → [HOOK] → Capture speaker audio
    │   ├── RecordTrack::stop()         → [HOOK] → Close mic file (.bc)
    │   └── Track::stop()                → [HOOK] → Close speaker file (.ac)
    └── libaudiohook.so (our injected library)
```

## Prerequisites

- Rooted Android device (Android 7.0+)
- Android NDK for building
- PLThook library (included)
- ADB with root access

## Building

### 1. Setup NDK Environment

```bash
export ANDROID_NDK=/path/to/android-ndk-r21e
export PATH=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
```

### 2. Get PLThook

```bash
git clone https://github.com/kubo/plthook.git
cp plthook/plthook.h plthook/plthook_elf.c ./
```

### 3. Build the Hook Library

```bash
# For 64-bit devices (most modern phones)
aarch64-linux-android29-clang++ -shared -fPIC -o libaudiohook.so \
    audioserver_hook.cpp plthook_elf.c \
    -llog -ldl -std=c++11 -O2

# For 32-bit devices
armv7a-linux-androideabi29-clang++ -shared -fPIC -o libaudiohook.so \
    audioserver_hook.cpp plthook_elf.c \
    -llog -ldl -std=c++11 -O2
```

### 4. Build the Injector (Optional)

```bash
aarch64-linux-android29-clang++ -o inject_audio inject_audio.cpp
```

## Deployment

### Method 1: LD_PRELOAD (Recommended)

```bash
# Push library to device
adb push libaudiohook.so /data/local/tmp/

# Stop audioserver
adb shell su -c 'stop media.audio_flinger'

# Set preload property
adb shell su -c 'setprop wrap.media.audio_flinger "LD_PRELOAD=/data/local/tmp/libaudiohook.so"'

# Restart audioserver
adb shell su -c 'start media.audio_flinger'

# Verify hooks are active
adb logcat -s AudioHook:*
```

### Method 2: Runtime Injection

```bash
# Push files
adb push libaudiohook.so /data/local/tmp/
adb push inject_audio /data/local/tmp/
adb shell chmod +x /data/local/tmp/inject_audio

# Inject into running process
adb shell su -c /data/local/tmp/inject_audio
```

### Method 3: Magisk Module (Persistent)

Create a Magisk module for system-wide persistent hooks:

```
audioserver-hook/
├── META-INF/
│   └── com/
│       └── google/
│           └── android/
│               ├── update-binary
│               └── updater-script
├── module.prop
├── service.sh
└── system/
    └── lib64/
        └── libaudiohook.so
```

## Usage

### 1. Start Capture

Once hooks are active, audio capture starts automatically for voice calls:

```bash
# Monitor live capture
./manage_audio.sh monitor
```

### 2. Retrieve Audio Files

```bash
# List captured sessions
./manage_audio.sh list

# Pull audio files from device
./manage_audio.sh pull

# Files are saved as:
# - .bc files: Uplink (microphone) PCM audio
# - .ac files: Downlink (speaker) PCM audio
```

### 3. Convert to WAV

```bash
# Convert PCM to playable WAV files
./manage_audio.sh convert

# Or manually with sox:
sox -r 48000 -e signed -b 16 -c 1 input.bc output_mic.wav
sox -r 48000 -e signed -b 16 -c 2 input.ac output_speaker.wav
```

## File Format

Raw PCM audio files:
- **Sample Rate**: Usually 48000 Hz (may vary by device)
- **Format**: 16-bit signed PCM (AUDIO_FORMAT_PCM_16_BIT)
- **Channels**: Mono for mic, Stereo for speaker
- **Byte Order**: Little-endian

## Customization

### Adjusting Offsets

If the hooks don't work on your device, you may need to adjust the structure offsets:

```cpp
// In audioserver_hook.cpp, update these based on your libaudioflinger.so:
constexpr size_t SAMPLERATE_OFFSET = 0x178;  // Your offset here
constexpr size_t UID_OFFSET = 0x234;
constexpr size_t FRAMESIZE_OFFSET = 0x184;
// ... etc
```

Find offsets using:
```bash
# Analyze libaudioflinger.so structure
adb pull /system/lib64/libaudioflinger.so
objdump -t libaudioflinger.so | grep TrackBase
readelf -s libaudioflinger.so | grep getNextBuffer
```

### Filtering Specific Apps

Modify the UID filter to target specific applications:

```cpp
// Add to hook functions:
const uint32_t WHATSAPP_UID = 10191;  // Find with: dumpsys package com.whatsapp
if (readUid(thisPtr) != WHATSAPP_UID) return ret;
```

### Changing Audio Attributes Filter

```cpp
// Current: Voice communication only
if (attr->source != AUDIO_SOURCE_VOICE_COMMUNICATION) return;

// Alternative: Capture all audio
// (remove the filter check entirely)

// Alternative: Media playback only
if (attr->usage != AUDIO_USAGE_MEDIA) return;
```

## Troubleshooting

### No Audio Captured
- Check hooks are loaded: `adb logcat -s AudioHook:*`
- Verify audioserver is running: `adb shell pidof audioserver`
- Ensure voice call is active during capture
- Check SELinux: `adb shell getenforce` (may need permissive mode)

### Wrong Audio Format
- Try different sample rates: 8000, 16000, 44100, 48000 Hz
- Check channel count: mono vs stereo
- Verify PCM format: 16-bit vs 24-bit

### Crashes or Instability
- Check symbol names match your Android version
- Verify structure offsets are correct
- Ensure proper memory alignment
- Review logcat for segfaults

## Security Considerations

⚠️ **Warning**: This tool intercepts audio communications. Use responsibly and legally:
- Only use on devices you own
- Respect privacy laws in your jurisdiction
- Do not use for unauthorized surveillance
- Intended for security research and debugging only

## Comparison with Frida

| Feature | C++ PLThook | Frida |
|---------|------------|-------|
| Performance | Very Low overhead | Higher overhead |
| Persistence | Can survive reboots | Requires re-injection |
| Stealth | Harder to detect | Easier to detect |
| Development | Compile-time | Runtime scripting |
| Debugging | More complex | Interactive REPL |
| Deployment | Native library | Python + JS |

## Advanced Usage

### Combining with Call Recording

Sync with telephony events for complete call recording:

```cpp
// Hook RIL or telephony service for call state
// Start/stop recording based on call events
```

### Real-time Streaming

Stream audio over network instead of saving to files:

```cpp
// In hook functions, add network streaming:
send_to_server(buffer->raw, bufferSize);
```

### Audio Processing

Apply filters or effects before saving:

```cpp
// Example: Noise reduction
apply_noise_filter(buffer->raw, bufferSize);
```

## License

This code is provided for educational and research purposes only. Users are responsible for complying with all applicable laws and regulations.

## Credits

- PLThook library by Kubo Takehiro
- Based on Android Open Source Project (AOSP) audioflinger
- Inspired by the original Frida implementation

## Support

For issues or questions:
1. Check existing hooks work: `adb logcat -s AudioHook:*`
2. Verify your Android version compatibility
3. Ensure proper offsets for your device
4. Review SELinux policies if applicable