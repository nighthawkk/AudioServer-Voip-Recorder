// injector.cpp
// C++ implementation of ptrace-based library injection for Android

#include "injector.hpp"
#include <android/log.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

namespace AndroidInjector {

// Android version-specific paths
#if defined(__aarch64__)
// ARM64 paths
const PathConfig PATHS_DEFAULT = {
    .libc = "/system/lib64/libc.so",
    .linker = "/system/lib64/libdl.so", 
    .vndk = "/system/lib64/libaudioflinger.so"  // Use libaudioflinger instead of libRS
};

const PathConfig PATHS_ANDROID_10 = {
    .libc = "/apex/com.android.runtime/lib64/bionic/libc.so",
    .linker = "/apex/com.android.runtime/lib64/bionic/libdl.so",
    .vndk = "/system/lib64/libaudioflinger.so"  // Audioflinger is still in /system on Android 10+
};

#elif defined(__arm__)
// ARM32 paths
const PathConfig PATHS_DEFAULT = {
    .libc = "/system/lib/libc.so",
    .linker = "/system/lib/libdl.so",
    .vndk = "/system/lib/libaudioflinger.so"
};

const PathConfig PATHS_ANDROID_10 = {
    .libc = "/apex/com.android.runtime/lib/bionic/libc.so",
    .linker = "/apex/com.android.runtime/lib/bionic/libdl.so",
    .vndk = "/system/lib/libaudioflinger.so"
};

#elif defined(__x86_64__)
// x86_64 paths
const PathConfig PATHS_DEFAULT = {
    .libc = "/system/lib64/libc.so",
    .linker = "/system/lib64/libdl.so",
    .vndk = "/system/lib64/libaudioflinger.so"
};

const PathConfig PATHS_ANDROID_10 = {
    .libc = "/apex/com.android.runtime/lib64/bionic/libc.so",
    .linker = "/apex/com.android.runtime/lib64/bionic/libdl.so",
    .vndk = "/system/lib64/libaudioflinger.so"
};

#elif defined(__i386__)
// x86 32-bit paths
const PathConfig PATHS_DEFAULT = {
    .libc = "/system/lib/libc.so",
    .linker = "/system/lib/libdl.so",
    .vndk = "/system/lib/libaudioflinger.so"
};

const PathConfig PATHS_ANDROID_10 = {
    .libc = "/apex/com.android.runtime/lib/bionic/libc.so",
    .linker = "/apex/com.android.runtime/lib/bionic/libdl.so",
    .vndk = "/system/lib/libaudioflinger.so"
};
#endif

Injector::Injector(bool debug) : debug_(debug), ptrace_(debug) {
    LOGD("Injector initialized (debug=%d)", debug);
}

Injector::~Injector() {
    // Cleanup if needed
}

long Injector::callMmap(pid_t pid, size_t length) {
    long function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_DEFAULT.libc, 
                                                      reinterpret_cast<long>(mmap));
    
    if (function_addr == 0) {
        // Try Android 10+ path
        function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_ANDROID_10.libc,
                                                     reinterpret_cast<long>(mmap));
    }
    
    if (function_addr == 0) {
        LOGE("Failed to find mmap in target process");
        return 0;
    }
    
    long params[6] = {
        0,                                  // addr
        static_cast<long>(length),          // length
        PROT_READ | PROT_WRITE,            // prot
        MAP_PRIVATE | MAP_ANONYMOUS,       // flags
        0,                                  // fd
        0                                   // offset
    };
    
    LOGD("mmap: func_addr=0x%lx pid=%d size=%zu", function_addr, pid, length);
    return ptrace_.callRemoteFunction(pid, function_addr, params, 6);
}

long Injector::callMunmap(pid_t pid, long addr, size_t length) {
    long function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_DEFAULT.libc,
                                                      reinterpret_cast<long>(munmap));
    
    if (function_addr == 0) {
        function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_ANDROID_10.libc,
                                                     reinterpret_cast<long>(munmap));
    }
    
    if (function_addr == 0) {
        LOGE("Failed to find munmap in target process");
        return -1;
    }
    
    long params[2] = {
        addr,
        static_cast<long>(length)
    };
    
    LOGD("munmap: func_addr=0x%lx pid=%d addr=0x%lx size=%zu", 
         function_addr, pid, addr, length);
    return ptrace_.callRemoteFunction(pid, function_addr, params, 2);
}

long Injector::callDlopen(pid_t pid, const std::string& library_path) {
    long function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_DEFAULT.linker,
                                                      reinterpret_cast<long>(dlopen));
    
    if (function_addr == 0) {
        function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_ANDROID_10.linker,
                                                     reinterpret_cast<long>(dlopen));
    }
    
    if (function_addr == 0) {
        LOGE("Failed to find dlopen in target process");
        return 0;
    }
    
    // Allocate memory in target process for library path
    long mmap_ret = callMmap(pid, 0x400);
    if (mmap_ret == 0) {
        LOGE("Failed to allocate memory in target process");
        return 0;
    }
    
    // Write library path to allocated memory
    ptrace_.writeMemory(pid, reinterpret_cast<uint8_t*>(mmap_ret),
                       reinterpret_cast<const uint8_t*>(library_path.c_str()),
                       library_path.length() + 1);
    
    long params[2] = {
        mmap_ret,
        RTLD_NOW | RTLD_LOCAL
    };
    
    LOGD("dlopen: func_addr=0x%lx pid=%d library=%s", 
         function_addr, pid, library_path.c_str());
    
    // Handle namespace restrictions (Android 7.0+)
    // Try to find a suitable library for return address
    long vndk_return_addr = 0;
    
    // List of libraries likely to be in audioserver
    const char* candidate_libs[] = {
        "/system/lib64/libaudioflinger.so",
        "/system/lib64/libbinder.so",
        "/system/lib64/libutils.so",
        "/system/lib64/libcutils.so",
        "/system/lib64/liblog.so",
        "/system/lib/libaudioflinger.so",  // 32-bit fallback
        "/system/lib/libbinder.so",
        nullptr
    };
    
    // Try each candidate until we find one that's loaded
    for (int i = 0; candidate_libs[i] != nullptr; i++) {
        vndk_return_addr = utils_.getModuleBaseAddr(pid, candidate_libs[i]);
        if (vndk_return_addr != 0) {
            LOGD("Using %s for return addr: 0x%lx", candidate_libs[i], vndk_return_addr);
            break;
        }
    }
    
    // If still no suitable library, try libc as last resort
    if (vndk_return_addr == 0) {
        vndk_return_addr = utils_.getModuleBaseAddr(pid, PATHS_DEFAULT.libc);
        LOGD("Using libc as fallback return addr: 0x%lx", vndk_return_addr);
    }
    
    // Perform the dlopen call
    long ret = 0;
    if (vndk_return_addr != 0) {
        // Use namespace bypass with a valid return address
        ret = ptrace_.callRemoteFunctionFromNamespace(pid, function_addr, 
                                                      vndk_return_addr, params, 2);
    } else {
        // For system processes, we might not need namespace bypass
        LOGD("Attempting dlopen without namespace bypass (system process)");
        ret = ptrace_.callRemoteFunction(pid, function_addr, params, 2);
    }
    
    // Clean up allocated memory
    LOGD("string: func_addr=0x%lx", 
        mmap_ret);

    // callMunmap(pid, mmap_ret, 0x400);
    
    return ret;
}

long Injector::callDlsym(pid_t pid, long so_handle, const std::string& symbol) {
    long function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_DEFAULT.linker,
                                                      reinterpret_cast<long>(dlsym));
    
    if (function_addr == 0) {
        function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_ANDROID_10.linker,
                                                     reinterpret_cast<long>(dlsym));
    }
    
    if (function_addr == 0) {
        LOGE("Failed to find dlsym in target process");
        return 0;
    }
    
    // Allocate memory for symbol name
    long mmap_ret = callMmap(pid, 0x400);
    if (mmap_ret == 0) {
        return 0;
    }
    
    ptrace_.writeMemory(pid, reinterpret_cast<uint8_t*>(mmap_ret),
                       reinterpret_cast<const uint8_t*>(symbol.c_str()),
                       symbol.length() + 1);
    
    long params[2] = {
        so_handle,
        mmap_ret
    };
    
    LOGD("dlsym: func_addr=0x%lx pid=%d handle=0x%lx symbol=%s",
         function_addr, pid, so_handle, symbol.c_str());
    
    long ret = ptrace_.callRemoteFunction(pid, function_addr, params, 2);
    callMunmap(pid, mmap_ret, 0x400);
    
    return ret;
}

long Injector::callDlclose(pid_t pid, long so_handle) {
    long function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_DEFAULT.linker,
                                                      reinterpret_cast<long>(dlclose));
    
    if (function_addr == 0) {
        function_addr = utils_.getRemoteFunctionAddr(pid, PATHS_ANDROID_10.linker,
                                                     reinterpret_cast<long>(dlclose));
    }
    
    if (function_addr == 0) {
        LOGE("Failed to find dlclose in target process");
        return -1;
    }
    
    long params[1] = { so_handle };
    
    LOGD("dlclose: func_addr=0x%lx pid=%d handle=0x%lx", 
         function_addr, pid, so_handle);
    
    return ptrace_.callRemoteFunction(pid, function_addr, params, 1);
}

InjectionResult Injector::inject(pid_t pid, const std::string& library_path) {
    InjectionResult result;
    result.success = false;
    result.pid = pid;
    result.library_path = library_path;
    
    LOGD("Starting injection: pid=%d library=%s", pid, library_path.c_str());
    
    // Attach to target process
    if (!ptrace_.attach(pid)) {
        result.error_msg = "Failed to attach to process";
        LOGE("%s", result.error_msg.c_str());
        return result;
    }
    
    // Load library
    result.so_handle = callDlopen(pid, library_path);
    
    if (result.so_handle == 0) {
        result.error_msg = "Failed to load library: " + std::string(strerror(errno));
        LOGE("%s", result.error_msg.c_str());
    } else {
        result.success = true;
        LOGD("Injection successful: handle=0x%lx", result.so_handle);
    }
    
    // Detach from process
    ptrace_.detach(pid);
    
    return result;
}

InjectionResult Injector::inject(const std::string& process_name, 
                                 const std::string& library_path) {
    pid_t pid = utils_.getPidByName(process_name);
    
    if (pid == -1) {
        InjectionResult result;
        result.success = false;
        result.error_msg = "Process not found: " + process_name;
        LOGE("%s", result.error_msg.c_str());
        return result;
    }
    
    return inject(pid, library_path);
}

bool Injector::unload(pid_t pid, long so_handle) {
    if (!ptrace_.attach(pid)) {
        LOGE("Failed to attach to process %d", pid);
        return false;
    }
    
    long ret = callDlclose(pid, so_handle);
    ptrace_.detach(pid);
    
    return ret == 0;
}

} // namespace AndroidInjector