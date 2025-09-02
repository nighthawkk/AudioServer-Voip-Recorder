// injector.hpp
// Header for C++ Android library injector

#ifndef INJECTOR_HPP_
#define INJECTOR_HPP_

#include <string>
#include <memory>
#include "ptrace_wrapper.hpp"
#include "process_utils.hpp"

// Logging macros
#ifdef DEBUG
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Injector", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Injector", __VA_ARGS__)
#else
#define LOGD(...)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Injector", __VA_ARGS__)
#endif

namespace AndroidInjector {

// Android path configuration
struct PathConfig {
    const char* libc;
    const char* linker;
    const char* vndk;
};

// Injection result structure
struct InjectionResult {
    bool success;
    pid_t pid;
    std::string library_path;
    long so_handle;
    std::string error_msg;
};

// Main injector class
class Injector {
public:
    explicit Injector(bool debug = false);
    ~Injector();
    
    // Main injection methods
    InjectionResult inject(pid_t pid, const std::string& library_path);
    InjectionResult inject(const std::string& process_name, const std::string& library_path);
    
    // Unload injected library
    bool unload(pid_t pid, long so_handle);
    
    // Individual remote call methods (public for advanced usage)
    long callMmap(pid_t pid, size_t length);
    long callMunmap(pid_t pid, long addr, size_t length);
    long callDlopen(pid_t pid, const std::string& library_path);
    long callDlsym(pid_t pid, long so_handle, const std::string& symbol);
    long callDlclose(pid_t pid, long so_handle);
    
private:
    bool debug_;
    PtraceWrapper ptrace_;
    ProcessUtils utils_;
};

} // namespace AndroidInjector

#endif // INJECTOR_HPP_