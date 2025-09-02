// main.cpp
// Main program for Android library injector

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <android/log.h>
#include "injector.hpp"

using namespace AndroidInjector;

void printUsage(const char* program_name) {
    std::cout << "Android Library Injector (C++ Version)\n";
    std::cout << "Usage: " << program_name << " [options] <pid|process_name> <library_path>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -p, --pid          First argument is PID (default: process name)\n";
    std::cout << "  -u, --unload       Unload library (requires handle with -h)\n";
    std::cout << "  -h, --handle=ADDR  Library handle for unloading\n";
    std::cout << "  -d, --debug        Enable debug output\n";
    std::cout << "  -l, --list         List all instances of process\n";
    std::cout << "  -i, --info         Show process information\n";
    std::cout << "  -s, --selinux      Check/set SELinux status\n";
    std::cout << "  -a, --audio        Inject audio hook library\n";
    std::cout << "  --help             Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " audioserver /data/local/tmp/libaudiohook.so\n";
    std::cout << "  " << program_name << " -p 1234 /data/local/tmp/libtest.so\n";
    std::cout << "  " << program_name << " -d com.whatsapp /data/local/tmp/libhook.so\n";
    std::cout << "  " << program_name << " -u -p 1234 -h 0x7f8a9b0000\n";
    std::cout << "  " << program_name << " -i audioserver\n";
    std::cout << "  " << program_name << " -a  # Quick inject audio hooks\n";
}

void showProcessInfo(const std::string& process_name) {
    ProcessUtils utils;
    pid_t pid = utils.getPidByName(process_name);
    
    if (pid == -1) {
        std::cerr << "Process not found: " << process_name << std::endl;
        return;
    }
    
    ProcessInfo info = utils.getProcessInfo(pid);
    
    std::cout << "Process Information:\n";
    std::cout << "  Name: " << info.name << "\n";
    std::cout << "  PID: " << info.pid << "\n";
    std::cout << "  UID: " << info.uid << "\n";
    std::cout << "  Loaded modules (" << info.modules.size() << "):\n";
    
    // Show first 10 modules
    int count = 0;
    for (const auto& module : info.modules) {
        std::cout << "    " << module << "\n";
        if (++count >= 10 && info.modules.size() > 10) {
            std::cout << "    ... and " << (info.modules.size() - 10) << " more\n";
            break;
        }
    }
}

void listProcessInstances(const std::string& process_name) {
    ProcessUtils utils;
    std::vector<pid_t> pids = utils.getAllPids(process_name);
    
    if (pids.empty()) {
        std::cout << "No instances of " << process_name << " found\n";
        return;
    }
    
    std::cout << "Found " << pids.size() << " instance(s) of " << process_name << ":\n";
    for (pid_t pid : pids) {
        ProcessInfo info = utils.getProcessInfo(pid);
        std::cout << "  PID: " << pid << " (UID: " << info.uid << ")\n";
    }
}

void checkSelinux() {
    ProcessUtils utils;
    bool enforcing = utils.isSelinuxEnforcing();
    
    std::cout << "SELinux status: " << (enforcing ? "Enforcing" : "Permissive") << "\n";
    
    if (enforcing) {
        std::cout << "Attempting to set permissive mode...\n";
        if (utils.setSelinuxPermissive()) {
            std::cout << "SELinux set to permissive\n";
        } else {
            std::cout << "Failed to set SELinux to permissive (need root)\n";
        }
    }
}

void quickAudioInject() {
    const char* AUDIO_HOOK_LIB = "/data/local/tmp/libaudiohook.so";
    const char* AUDIOSERVER = "audioserver";
    
    std::cout << "Quick audio hook injection\n";
    std::cout << "Target: " << AUDIOSERVER << "\n";
    std::cout << "Library: " << AUDIO_HOOK_LIB << "\n";
    
    Injector injector(true);
    InjectionResult result = injector.inject(AUDIOSERVER, AUDIO_HOOK_LIB);
    
    if (result.success) {
        std::cout << "SUCCESS: Audio hooks injected\n";
        std::cout << "Handle: 0x" << std::hex << result.so_handle << std::dec << "\n";
        std::cout << "Monitor with: adb logcat -s AudioHook:*\n";
    } else {
        std::cerr << "FAILED: " << result.error_msg << "\n";
    }
}

int main(int argc, char* argv[]) {
    bool use_pid = false;
    bool unload = false;
    bool debug = false;
    bool show_info = false;
    bool list_instances = false;
    bool check_selinux = false;
    bool audio_inject = false;
    long unload_handle = 0;
    
    static struct option long_options[] = {
        {"pid",     no_argument,       0, 'p'},
        {"unload",  no_argument,       0, 'u'},
        {"handle",  required_argument, 0, 'h'},
        {"debug",   no_argument,       0, 'd'},
        {"info",    no_argument,       0, 'i'},
        {"list",    no_argument,       0, 'l'},
        {"selinux", no_argument,       0, 's'},
        {"audio",   no_argument,       0, 'a'},
        {"help",    no_argument,       0, '?'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "puh:dilsa?", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                use_pid = true;
                break;
            case 'u':
                unload = true;
                break;
            case 'h':
                unload_handle = std::strtoul(optarg, nullptr, 0);
                break;
            case 'd':
                debug = true;
                break;
            case 'i':
                show_info = true;
                break;
            case 'l':
                list_instances = true;
                break;
            case 's':
                check_selinux = true;
                break;
            case 'a':
                audio_inject = true;
                break;
            case '?':
            default:
                printUsage(argv[0]);
                return 0;
        }
    }
    
    // Handle special operations
    if (check_selinux) {
        checkSelinux();
        return 0;
    }
    
    if (audio_inject) {
        quickAudioInject();
        return 0;
    }
    
    // Check remaining arguments
    int remaining_args = argc - optind;
    
    if (show_info || list_instances) {
        if (remaining_args < 1) {
            std::cerr << "Error: Process name required\n";
            return 1;
        }
        
        std::string process_name = argv[optind];
        
        if (show_info) {
            showProcessInfo(process_name);
        }
        if (list_instances) {
            listProcessInstances(process_name);
        }
        return 0;
    }
    
    if (remaining_args < 2 && !unload) {
        std::cerr << "Error: Insufficient arguments\n\n";
        printUsage(argv[0]);
        return 1;
    }
    
    // Create injector
    Injector injector(debug);
    
    // Handle unload operation
    if (unload) {
        if (unload_handle == 0) {
            std::cerr << "Error: Handle required for unload operation\n";
            return 1;
        }
        
        pid_t pid;
        if (use_pid) {
            pid = std::strtoul(argv[optind], nullptr, 10);
        } else {
            ProcessUtils utils;
            pid = utils.getPidByName(argv[optind]);
        }
        
        if (pid == -1) {
            std::cerr << "Error: Process not found\n";
            return 1;
        }
        
        std::cout << "Unloading library from PID " << pid << "...\n";
        if (injector.unload(pid, unload_handle)) {
            std::cout << "Successfully unloaded\n";
            return 0;
        } else {
            std::cerr << "Failed to unload\n";
            return 1;
        }
    }
    
    // Normal injection
    std::string library_path = argv[optind + 1];
    InjectionResult result;
    
    if (use_pid) {
        pid_t pid = std::strtoul(argv[optind], nullptr, 10);
        std::cout << "Injecting into PID: " << pid << "\n";
        std::cout << "Library: " << library_path << "\n";
        result = injector.inject(pid, library_path);
    } else {
        std::string process_name = argv[optind];
        std::cout << "Injecting into process: " << process_name << "\n";
        std::cout << "Library: " << library_path << "\n";
        result = injector.inject(process_name, library_path);
    }
    
    if (result.success) {
        std::cout << "\n=== Injection Successful ===\n";
        std::cout << "PID: " << result.pid << "\n";
        std::cout << "Library: " << result.library_path << "\n";
        std::cout << "Handle: 0x" << std::hex << result.so_handle << std::dec << "\n";
    } else {
        std::cerr << "\n=== Injection Failed ===\n";
        std::cerr << "Error: " << result.error_msg << "\n";
        return 1;
    }
    
    return 0;
}