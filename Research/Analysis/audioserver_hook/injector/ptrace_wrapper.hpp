// ptrace_wrapper.hpp
// Header for ptrace wrapper class

#ifndef PTRACE_WRAPPER_HPP_
#define PTRACE_WRAPPER_HPP_

#include <cstdint>
#include <cstddef>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <linux/elf.h>
#include <sys/user.h>

// Architecture-specific register structure definitions
#if defined(__aarch64__)
    #define pt_regs user_pt_regs
#elif defined(__x86_64__)
    #define pt_regs user_regs_struct
#elif defined(__i386__)
    #define pt_regs user_regs_struct
#elif defined(__arm__)
    // ARM32: pt_regs is already correctly defined in sys/user.h
#endif

namespace AndroidInjector {

// Logging macros (defined in injector.hpp, redefined here if needed)
#ifndef LOGD
#ifdef DEBUG
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Ptrace", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Ptrace", __VA_ARGS__)
#else
#define LOGD(...)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Ptrace", __VA_ARGS__)
#endif
#endif

class PtraceWrapper {
public:
    explicit PtraceWrapper(bool debug = false);
    ~PtraceWrapper();
    
    // Process control
    bool attach(pid_t pid);
    bool detach(pid_t pid);
    
    // Memory operations
    void writeMemory(pid_t pid, uint8_t* addr, const uint8_t* data, size_t size);
    void readMemory(pid_t pid, uint8_t* addr, uint8_t* buffer, size_t size);
    
    // Remote function calling
    long callRemoteFunction(pid_t pid, long function_addr, long* args, size_t argc);
    long callRemoteFunctionFromNamespace(pid_t pid, long function_addr, 
                                        long return_addr, long* args, size_t argc);
    
private:
    bool debug_;
    
    // Internal helpers - using pt_regs which is now properly defined
    void getRegs(pid_t pid, struct pt_regs* regs);
    void setRegs(pid_t pid, struct pt_regs* regs);
    void cont(pid_t pid);
};

} // namespace AndroidInjector

#endif // PTRACE_WRAPPER_HPP_