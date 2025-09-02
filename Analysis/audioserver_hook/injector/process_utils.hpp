// process_utils.hpp
// Process utilities header

#ifndef PROCESS_UTILS_HPP_
#define PROCESS_UTILS_HPP_

#include <string>
#include <vector>
#include <set>
#include <sys/types.h>

namespace AndroidInjector {

// Logging macros
#ifndef LOGD
#ifdef DEBUG
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "ProcessUtils", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ProcessUtils", __VA_ARGS__)
#else
#define LOGD(...)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ProcessUtils", __VA_ARGS__)
#endif
#endif

// Process information structure
struct ProcessInfo {
    pid_t pid;
    uid_t uid;
    std::string name;
    std::vector<std::string> modules;
    bool valid;
};

class ProcessUtils {
public:
    ProcessUtils();
    ~ProcessUtils();
    
    // Process discovery
    pid_t getPidByName(const std::string& process_name);
    std::vector<pid_t> getAllPids(const std::string& process_name);
    
    // Module address resolution
    long getModuleBaseAddr(pid_t pid, const std::string& module_name);
    long getRemoteFunctionAddr(pid_t remote_pid, const std::string& module_name, 
                              long local_function_addr);
    
    // Process information
    ProcessInfo getProcessInfo(pid_t pid);
    
    // SELinux management
    bool isSelinuxEnforcing();
    bool setSelinuxPermissive();
    
private:
    // Helper methods can be added here
};

} // namespace AndroidInjector

#endif // PROCESS_UTILS_HPP_