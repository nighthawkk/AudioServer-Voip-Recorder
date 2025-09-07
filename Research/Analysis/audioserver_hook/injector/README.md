# Android C++ Injector

A modern C++ implementation of the Android ptrace-based library injector, converted from the original C code. Features namespace handling for Android 7.0+ and automatic path detection for Android 10+.

## Features

- **Full ptrace-based injection** - Remote function calling via ptrace
- **Multi-architecture support** - ARM64, ARM32, x86_64, x86
- **Android 10+ support** - Automatic APEX path detection
- **Namespace bypass** - Handles linker namespace restrictions
- **Emulator compatible** - Full x86_64 support for AVD testing
- **Process utilities** - PID lookup, module enumeration, SELinux management
- **Audio hook integration** - Quick injection for audioserver hooking
- **C++14 implementation** - Clean, object-oriented design
- **Debug mode** - Comprehensive logging for troubleshooting

## Architecture

```
AndroidInjector/
├── Injector              # Main injection orchestrator
├── PtraceWrapper         # Low-level ptrace operations
├── ProcessUtils          # Process discovery and analysis
└── PathConfig            # Android version-specific paths
```

## Building

### Prerequisites

```bash
export ANDROID_NDK=/path/to/android-ndk-r21e
```

### Quick Build

```bash
# Build everything
./build_injector.sh all

# Or step by step:
./build_injector.sh build      # Build injector
./build_injector.sh combined   # Build with audio hook
./build_injector.sh deploy     # Push to device
```

### Manual Build

```bash
# Using NDK
$ANDROID_NDK/ndk-build

# Using standalone toolchain - ARM64
$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++ \
    -o injector \
    main.cpp injector.cpp ptrace_wrapper.cpp process_utils.cpp \
    -llog -std=c++14 -O2 -static

# x86_64 for emulator
$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android29-clang++ \
    -o injector_x86_64 \
    main.cpp injector.cpp ptrace_wrapper.cpp process_utils.cpp \
    -llog -std=c++14 -O2 -static
```

## Usage

### Basic Injection

```bash
# Inject by process name
./injector audioserver /data/local/tmp/libaudiohook.so

# Inject by PID
./injector -p 1234 /data/local/tmp/libtest.so

# Debug mode
./injector -d com.whatsapp /data/local/tmp/libhook.so
```

### Audio Hook Quick Injection

```bash
# One-command audio hook injection
./injector -a

# Equivalent to:
./injector audioserver /data/local/tmp/libaudiohook.so
```

### Process Information

```bash
# Show process info
./injector -i audioserver

# List all instances
./injector -l com.android.phone

# Check SELinux status
./injector -s
```

### Library Unloading

```bash
# Unload by handle
./injector -u -p 1234 -h 0x7f8a9b0000
```

## C++ API Usage

```cpp
#include "injector.hpp"

using namespace AndroidInjector;

int main() {
    // Create injector
    Injector injector(true);  // true = debug mode
    
    // Inject by process name
    InjectionResult result = injector.inject("audioserver", 
                                            "/data/local/tmp/libaudiohook.so");
    
    if (result.success) {
        std::cout << "Injected! Handle: 0x" 
                  << std::hex << result.so_handle << std::endl;
    } else {
        std::cerr << "Failed: " << result.error_msg << std::endl;
    }
    
    // Inject by PID
    pid_t pid = 1234;
    result = injector.inject(pid, "/data/local/tmp/libtest.so");
    
    // Unload library
    injector.unload(pid, result.so_handle);
    
    return 0;
}
```

## Advanced Features

### Process Discovery

```cpp
ProcessUtils utils;

// Find process by name
pid_t pid = utils.getPidByName("audioserver");

// Get all instances
std::vector<pid_t> pids = utils.getAllPids("com.android.phone");

// Get process info
ProcessInfo info = utils.getProcessInfo(pid);
std::cout << "Process: " << info.name << " (UID: " << info.uid << ")" << std::endl;

// List loaded modules
for (const auto& module : info.modules) {
    std::cout << "  " << module << std::endl;
}
```

### Module Address Resolution

```cpp
// Get module base address
long base = utils.getModuleBaseAddr(pid, "/system/lib64/libc.so");

// Calculate remote function address
long remote_mmap = utils.getRemoteFunctionAddr(pid, "/system/lib64/libc.so", 
                                               (long)mmap);
```

### Remote Function Calls

```cpp
PtraceWrapper ptrace;

// Attach to process
ptrace.attach(pid);

// Call remote function
long params[2] = {0x1000, PROT_READ | PROT_WRITE};
long result = ptrace.callRemoteFunction(pid, remote_mmap, params, 2);

// Detach
ptrace.detach(pid);
```

## Architecture Support

| Architecture | Description | Use Case | Status |
|-------------|-------------|----------|---------|
| arm64-v8a | 64-bit ARM | Modern phones/tablets | ✅ Full Support |
| armeabi-v7a | 32-bit ARM | Older devices | ✅ Full Support |
| x86_64 | 64-bit Intel | Emulator (AVD), Intel tablets | ✅ Full Support |
| x86 | 32-bit Intel | 32-bit emulators | ✅ Full Support |

## Android Version Compatibility

| Android Version | API Level | Status | Notes |
|----------------|-----------|---------|-------|
| Android 14 | 34 | ✅ Supported | Latest APEX paths |
| Android 13 | 33 | ✅ Supported | |
| Android 12 | 31-32 | ✅ Supported | |
| Android 11 | 30 | ✅ Supported | |
| Android 10 | 29 | ✅ Supported | APEX paths auto-detected |
| Android 9 | 28 | ✅ Supported | |
| Android 8 | 26-27 | ✅ Supported | Namespace handling |
| Android 7 | 24-25 | ✅ Supported | Minimum version |
| Android 6 | 23 | ⚠️ Limited | No namespace bypass |

## Path Resolution

The injector automatically tries multiple paths for system libraries:

```cpp
// Android 10+ (APEX)
/apex/com.android.runtime/lib64/bionic/libc.so
/apex/com.android.runtime/lib64/bionic/libdl.so

// Android 9 and below
/system/lib64/libc.so
/system/lib64/libdl.so
```

## Troubleshooting

### "Failed to attach to process"
- Check root access: `su`
- Verify process exists: `ps -A | grep processname`
- Check SELinux: `getenforce` (set to Permissive if needed)

### "Failed to find dlopen"
- Verify Android version compatibility
- Check library paths in `/proc/PID/maps`
- Try both APEX and system paths

### "Library not loaded"
- Check library architecture matches process
- Verify library dependencies: `readelf -d library.so`
- Check logcat for dlopen errors: `adb logcat | grep dlopen`

### Namespace Restrictions
- The injector handles namespace restrictions automatically
- Uses VNDK library return address for bypassing
- Falls back to multiple strategies if needed

## Comparison: C vs C++

| Feature | Original C | C++ Version |
|---------|-----------|-------------|
| Code Structure | Procedural | Object-oriented |
| Error Handling | Return codes | Exceptions + Result objects |
| Memory Management | Manual | RAII + smart pointers |
| String Handling | char* | std::string |
| Process Discovery | Basic | Enhanced with vectors/sets |
| Debug Output | printf | Configurable logging |
| API | Functions | Classes with methods |

## File Structure

```
injector_project/
├── Source Files
│   ├── main.cpp              # CLI interface
│   ├── injector.cpp          # Main injector implementation
│   ├── ptrace_wrapper.cpp    # Ptrace operations (multi-arch)
│   └── process_utils.cpp     # Process utilities
│
├── Headers
│   ├── injector.hpp          # Injector class
│   ├── ptrace_wrapper.hpp    # Ptrace wrapper
│   └── process_utils.hpp     # Process utilities
│
├── Build Files
│   ├── Android.mk            # NDK build
│   ├── Application_injector.mk
│   ├── CMakeLists.txt        # CMake build
│   └── build_injector.sh     # Build script
│
├── Testing
│   └── test_emulator.sh      # x86_64 emulator test script
│
└── Output
    ├── injector_arm64        # ARM64 executable
    ├── injector_arm32        # ARM32 executable
    ├── injector_x86_64       # x86_64 executable (emulator)
    ├── injector_x86          # x86 executable
    └── libaudiohook.so       # Audio hook library (all archs)
```

## Security Considerations

⚠️ **Root Required**: This tool requires root access to function
⚠️ **System Modification**: Can inject code into system processes
⚠️ **Legal Compliance**: Only use on devices you own
⚠️ **Privacy**: Respect privacy laws when intercepting audio

## Contributing

The C++ version maintains full compatibility with the original C implementation while providing:
- Better error handling and reporting
- Cleaner API for integration
- Enhanced debugging capabilities
- Modern C++ best practices

## License

For educational and research purposes only. Users are responsible for complying with all applicable laws and regulations.