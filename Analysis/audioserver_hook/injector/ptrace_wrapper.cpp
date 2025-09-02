// ptrace_wrapper.cpp
// C++ wrapper for ptrace operations

#include "ptrace_wrapper.hpp"
#include <linux/elf.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <unistd.h>
#include <android/log.h>
#include <cstring>
#include <cerrno>

namespace AndroidInjector {

// Architecture-specific register access definitions
// Note: pt_regs is already defined in header
#if defined(__aarch64__)
    #define uregs    regs
    #define ARM_r0   regs[0]
    #define ARM_lr   regs[30]
    #define ARM_sp   sp
    #define ARM_pc   pc
    #define ARM_cpsr pstate
#elif defined(__arm__)
    // 32-bit ARM definitions
    #define ARM_r0   uregs[0]
    #define ARM_lr   uregs[14]
    #define ARM_sp   uregs[13]
    #define ARM_pc   uregs[15]
    #define ARM_cpsr uregs[16]
#elif defined(__i386__)
    // x86 32-bit
    #define ARM_r0   eax
    #define ARM_lr   0  // No link register on x86
    #define ARM_sp   esp
    #define ARM_pc   eip
    #define ARM_cpsr eflags
#elif defined(__x86_64__)
    // x86_64 definitions
    #define ARM_r0   rax
    #define ARM_lr   0  // No link register on x86_64
    #define ARM_sp   rsp
    #define ARM_pc   rip
    #define ARM_cpsr eflags
#endif

#define CPSR_T_MASK (1u << 5)

PtraceWrapper::PtraceWrapper(bool debug) : debug_(debug) {}

PtraceWrapper::~PtraceWrapper() {}

bool PtraceWrapper::attach(pid_t pid) {
    if (pid == -1) {
        return false;
    }
    
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
        LOGE("PTRACE_ATTACH failed: %s", strerror(errno));
        return false;
    }
    
    waitpid(pid, nullptr, WUNTRACED);
    
    if (debug_) {
        LOGD("Attached to process %d", pid);
    }
    
    return true;
}

bool PtraceWrapper::detach(pid_t pid) {
    if (pid == -1) {
        return false;
    }
    
    if (ptrace(PTRACE_DETACH, pid, nullptr, nullptr) < 0) {
        LOGE("PTRACE_DETACH failed: %s", strerror(errno));
        return false;
    }
    
    if (debug_) {
        LOGD("Detached from process %d", pid);
    }
    
    return true;
}

void PtraceWrapper::writeMemory(pid_t pid, uint8_t* addr, const uint8_t* data, size_t size) {
    const size_t WORD_SIZE = sizeof(long);
    int mod = size % WORD_SIZE;
    int loop_count = size / WORD_SIZE;
    uint8_t* tmp_addr = addr;
    const uint8_t* tmp_data = data;
    
    // Write full words
    for (int i = 0; i < loop_count; ++i) {
        ptrace(PTRACE_POKEDATA, pid, tmp_addr, *reinterpret_cast<const long*>(tmp_data));
        tmp_addr += WORD_SIZE;
        tmp_data += WORD_SIZE;
    }
    
    // Write remaining bytes
    if (mod > 0) {
        long val = ptrace(PTRACE_PEEKDATA, pid, tmp_addr, nullptr);
        uint8_t* p = reinterpret_cast<uint8_t*>(&val);
        for (int i = 0; i < mod; ++i) {
            p[i] = tmp_data[i];
        }
        ptrace(PTRACE_POKEDATA, pid, tmp_addr, val);
    }
    
    if (debug_) {
        LOGD("Wrote %zu bytes to %p in process %d", size, addr, pid);
    }
}

void PtraceWrapper::readMemory(pid_t pid, uint8_t* addr, uint8_t* buffer, size_t size) {
    const size_t WORD_SIZE = sizeof(long);
    int mod = size % WORD_SIZE;
    int loop_count = size / WORD_SIZE;
    uint8_t* tmp_addr = addr;
    uint8_t* tmp_buffer = buffer;
    
    // Read full words
    for (int i = 0; i < loop_count; ++i) {
        long word = ptrace(PTRACE_PEEKDATA, pid, tmp_addr, nullptr);
        memcpy(tmp_buffer, &word, WORD_SIZE);
        tmp_addr += WORD_SIZE;
        tmp_buffer += WORD_SIZE;
    }
    
    // Read remaining bytes
    if (mod > 0) {
        long word = ptrace(PTRACE_PEEKDATA, pid, tmp_addr, nullptr);
        memcpy(tmp_buffer, &word, mod);
    }
    
    if (debug_) {
        LOGD("Read %zu bytes from %p in process %d", size, addr, pid);
    }
}

void PtraceWrapper::getRegs(pid_t pid, struct pt_regs* regs) {
#if defined(__aarch64__) || defined(__arm__)
    struct {
        void* ufb;
        size_t len;
    } regsvec = { regs, sizeof(struct pt_regs) };
    
    ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &regsvec);
#elif defined(__i386__) || defined(__x86_64__)
    ptrace(PTRACE_GETREGS, pid, nullptr, regs);
#endif
}

void PtraceWrapper::setRegs(pid_t pid, struct pt_regs* regs) {
#if defined(__aarch64__) || defined(__arm__)
    struct {
        void* ufb;
        size_t len;
    } regsvec = { regs, sizeof(struct pt_regs) };
    
    ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &regsvec);
#elif defined(__i386__) || defined(__x86_64__)
    ptrace(PTRACE_SETREGS, pid, nullptr, regs);
#endif
}

void PtraceWrapper::cont(pid_t pid) {
    ptrace(PTRACE_CONT, pid, nullptr, nullptr);
}

long PtraceWrapper::callRemoteFunction(pid_t pid, long function_addr, long* args, size_t argc) {
    return callRemoteFunctionFromNamespace(pid, function_addr, 0, args, argc);
}

long PtraceWrapper::callRemoteFunctionFromNamespace(pid_t pid, long function_addr, 
                                                    long return_addr, long* args, size_t argc) {
#if defined(__aarch64__)
    constexpr size_t REGS_ARG_NUM = 8;  // ARM64 uses x0-x7 for arguments
#elif defined(__arm__)
    constexpr size_t REGS_ARG_NUM = 4;  // ARM32 uses r0-r3
#elif defined(__i386__)
    constexpr size_t REGS_ARG_NUM = 0;  // x86 uses stack for all arguments
#elif defined(__x86_64__)
    constexpr size_t REGS_ARG_NUM = 6;  // x86_64 uses rdi,rsi,rdx,rcx,r8,r9
#endif
    
    struct pt_regs regs, backup_regs;
    
    // Backup original registers
    getRegs(pid, &regs);
    memcpy(&backup_regs, &regs, sizeof(struct pt_regs));
    
#if defined(__aarch64__)
    // ARM64: Set up arguments in x0-x7 registers
    for (size_t i = 0; i < argc && i < REGS_ARG_NUM; ++i) {
        regs.regs[i] = args[i];
    }
    
    // Push remaining arguments to stack
    if (argc > REGS_ARG_NUM) {
        regs.sp -= (argc - REGS_ARG_NUM) * sizeof(long);
        long* stack_args = args + REGS_ARG_NUM;
        writeMemory(pid, reinterpret_cast<uint8_t*>(regs.sp),
                   reinterpret_cast<uint8_t*>(stack_args),
                   (argc - REGS_ARG_NUM) * sizeof(long));
    }
    
    // Set up return address and PC
    regs.regs[30] = return_addr;  // lr
    regs.pc = function_addr;
    
#elif defined(__arm__)
    // ARM32: Set up arguments in r0-r3 registers
    for (size_t i = 0; i < argc && i < REGS_ARG_NUM; ++i) {
        regs.uregs[i] = args[i];
    }
    
    // Push remaining arguments to stack
    if (argc > REGS_ARG_NUM) {
        regs.ARM_sp -= (argc - REGS_ARG_NUM) * sizeof(long);
        long* stack_args = args + REGS_ARG_NUM;
        writeMemory(pid, reinterpret_cast<uint8_t*>(regs.ARM_sp),
                   reinterpret_cast<uint8_t*>(stack_args),
                   (argc - REGS_ARG_NUM) * sizeof(long));
    }
    
    // Set up return address and PC
    regs.ARM_lr = return_addr;
    regs.ARM_pc = function_addr;
    
    // Handle thumb mode
    if (regs.ARM_pc & 1) {
        regs.ARM_pc &= (~1u);
        regs.ARM_cpsr |= CPSR_T_MASK;
    } else {
        regs.ARM_cpsr &= ~CPSR_T_MASK;
    }
    
#elif defined(__i386__)
    // x86: All arguments on stack
    regs.esp -= (argc + 1) * sizeof(long);  // +1 for return address
    
    // Write return address
    long ret_addr = return_addr ? return_addr : 0;
    writeMemory(pid, reinterpret_cast<uint8_t*>(regs.esp),
               reinterpret_cast<uint8_t*>(&ret_addr), sizeof(long));
    
    // Write arguments
    if (argc > 0) {
        writeMemory(pid, reinterpret_cast<uint8_t*>(regs.esp + sizeof(long)),
                   reinterpret_cast<uint8_t*>(args), argc * sizeof(long));
    }
    
    regs.eip = function_addr;
    
#elif defined(__x86_64__)
    // x86_64: First 6 args in registers (System V AMD64 ABI)
    // rdi, rsi, rdx, rcx, r8, r9
    if (argc > 0) regs.rdi = args[0];
    if (argc > 1) regs.rsi = args[1];
    if (argc > 2) regs.rdx = args[2];
    if (argc > 3) regs.rcx = args[3];
    if (argc > 4) regs.r8  = args[4];
    if (argc > 5) regs.r9  = args[5];
    
    // Push remaining arguments to stack
    if (argc > REGS_ARG_NUM) {
        // Align stack to 16 bytes
        size_t stack_args_size = (argc - REGS_ARG_NUM) * sizeof(long);
        size_t stack_adjustment = ((stack_args_size + 15) / 16) * 16 + 8; // +8 for return address
        regs.rsp -= stack_adjustment;
        
        // Write return address
        long ret_addr = return_addr ? return_addr : 0;
        writeMemory(pid, reinterpret_cast<uint8_t*>(regs.rsp),
                   reinterpret_cast<uint8_t*>(&ret_addr), sizeof(long));
        
        // Write stack arguments
        long* stack_args = args + REGS_ARG_NUM;
        writeMemory(pid, reinterpret_cast<uint8_t*>(regs.rsp + 8),
                   reinterpret_cast<uint8_t*>(stack_args),
                   (argc - REGS_ARG_NUM) * sizeof(long));
    } else {
        // Just push return address
        regs.rsp -= 8;
        long ret_addr = return_addr ? return_addr : 0;
        writeMemory(pid, reinterpret_cast<uint8_t*>(regs.rsp),
                   reinterpret_cast<uint8_t*>(&ret_addr), sizeof(long));
    }
    
    regs.rip = function_addr;
#endif
    
    // Execute function
    setRegs(pid, &regs);
    cont(pid);
    waitpid(pid, nullptr, WUNTRACED);
    
    // Get return value
    getRegs(pid, &regs);
    
    // Restore original registers
    setRegs(pid, &backup_regs);
    
    long return_value = 0;
#if defined(__aarch64__)
    return_value = regs.regs[0];  // x0
#elif defined(__arm__)
    return_value = regs.uregs[0];  // r0
#elif defined(__i386__)
    return_value = regs.eax;
#elif defined(__x86_64__)
    return_value = regs.rax;
#endif
    
    if (debug_) {
        LOGD("Called remote function 0x%lx with %zu args, returned 0x%llx",
             function_addr, argc, static_cast<long long>(return_value));
    }
    
    return return_value;
}

} // namespace AndroidInjector