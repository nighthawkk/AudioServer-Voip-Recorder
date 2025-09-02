// process_utils.cpp
// Process utilities for Android injection

#include "process_utils.hpp"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <android/log.h>
#include <unistd.h>
#include <cstring>

namespace AndroidInjector {

ProcessUtils::ProcessUtils() {}

ProcessUtils::~ProcessUtils() {}

pid_t ProcessUtils::getPidByName(const std::string& process_name) {
    if (process_name.empty()) {
        return -1;
    }
    
    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        LOGE("Failed to open /proc");
        return -1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Check if directory name is a number (PID)
        pid_t pid = 0;
        try {
            pid = std::stoi(entry->d_name);
        } catch (...) {
            continue;
        }
        
        if (pid > 0) {
            std::string cmdline_path = "/proc/" + std::string(entry->d_name) + "/cmdline";
            std::ifstream cmdline(cmdline_path);
            
            if (cmdline.is_open()) {
                std::string name;
                std::getline(cmdline, name, '\0');
                
                if (name == process_name) {
                    closedir(dir);
                    return pid;
                }
            }
        }
    }
    
    closedir(dir);
    return -1;
}

std::vector<pid_t> ProcessUtils::getAllPids(const std::string& process_name) {
    std::vector<pid_t> pids;
    
    if (process_name.empty()) {
        return pids;
    }
    
    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        return pids;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        pid_t pid = 0;
        
        pid = std::stoi(entry->d_name);
        
        
        if (pid > 0) {
            std::string cmdline_path = "/proc/" + std::string(entry->d_name) + "/cmdline";
            std::ifstream cmdline(cmdline_path);
            
            if (cmdline.is_open()) {
                std::string name;
                std::getline(cmdline, name, '\0');
                
                if (name == process_name) {
                    pids.push_back(pid);
                }
            }
        }
    }
    
    closedir(dir);
    return pids;
}

long ProcessUtils::getModuleBaseAddr(pid_t pid, const std::string& module_name) {
    if (pid == -1 || module_name.empty()) {
        return 0;
    }
    
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream maps(maps_path);
    
    if (!maps.is_open()) {
        LOGE("Failed to open %s", maps_path.c_str());
        return 0;
    }
    
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(module_name) != std::string::npos) {
            // Parse base address (format: address-address perms offset dev inode pathname)
            size_t dash_pos = line.find('-');
            if (dash_pos != std::string::npos) {
                std::string addr_str = line.substr(0, dash_pos);
                return std::stoul(addr_str, nullptr, 16);
                
            }
        }
    }
    
    return 0;
}

long ProcessUtils::getRemoteFunctionAddr(pid_t remote_pid, const std::string& module_name, 
                                         long local_function_addr) {
    pid_t local_pid = getpid();
    
    long local_base = getModuleBaseAddr(local_pid, module_name);
    long remote_base = getModuleBaseAddr(remote_pid, module_name);
    
    LOGD("Module: %s, local_base: 0x%lx, remote_base: 0x%lx",
         module_name.c_str(), local_base, remote_base);
    
    if (local_base == 0 || remote_base == 0) {
        return 0;
    }
    
    return local_function_addr + (remote_base - local_base);
}

ProcessInfo ProcessUtils::getProcessInfo(pid_t pid) {
    ProcessInfo info;
    info.pid = pid;
    info.valid = false;
    
    // Get process name
    std::string cmdline_path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream cmdline(cmdline_path);
    if (cmdline.is_open()) {
        std::getline(cmdline, info.name, '\0');
        info.valid = true;
    }
    
    // Get UID
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream status(status_path);
    if (status.is_open()) {
        std::string line;
        while (std::getline(status, line)) {
            if (line.find("Uid:") == 0) {
                std::istringstream iss(line);
                std::string uid_label;
                iss >> uid_label >> info.uid;
                break;
            }
        }
    }
    
    // Get loaded modules
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream maps(maps_path);
    if (maps.is_open()) {
        std::string line;
        std::set<std::string> unique_modules;
        
        while (std::getline(maps, line)) {
            // Extract pathname from maps line
            size_t path_start = line.find('/');
            if (path_start != std::string::npos) {
                std::string path = line.substr(path_start);
                // Remove trailing whitespace
                path.erase(path.find_last_not_of(" \t\n\r") + 1);
                unique_modules.insert(path);
            }
        }
        
        info.modules.assign(unique_modules.begin(), unique_modules.end());
    }
    
    return info;
}

bool ProcessUtils::isSelinuxEnforcing() {
    std::ifstream enforce("/sys/fs/selinux/enforce");
    if (enforce.is_open()) {
        int value;
        enforce >> value;
        return value == 1;
    }
    return false;
}

bool ProcessUtils::setSelinuxPermissive() {
    std::ofstream enforce("/sys/fs/selinux/enforce");
    if (enforce.is_open()) {
        enforce << "0";
        return true;
    }
    return false;
}

} // namespace AndroidInjector