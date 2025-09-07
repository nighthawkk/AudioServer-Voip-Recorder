#include <jni.h>
#include <string>
#include <android/log.h>
#include <dirent.h>

#define LOG_TAG "AudioHook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


std::string run_with_su(const char* cmd) {
    std::string command = "su -c \"" + std::string(cmd) + " 2>&1\"";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "Failed to run command";
    }

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    pclose(pipe);
    return result;
}

std::string getNativeLibDir(JNIEnv* env, jobject context) {
    jclass contextCls = env->GetObjectClass(context);

    jmethodID getAppInfo = env->GetMethodID(contextCls, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    jobject appInfo = env->CallObjectMethod(context, getAppInfo);

    jclass appInfoCls = env->GetObjectClass(appInfo);
    jfieldID nativeLibDirField = env->GetFieldID(appInfoCls, "nativeLibraryDir", "Ljava/lang/String;");
    jstring libDirJStr = (jstring) env->GetObjectField(appInfo, nativeLibDirField);

    const char* libDirCStr = env->GetStringUTFChars(libDirJStr, nullptr);
    std::string libDir(libDirCStr);
    env->ReleaseStringUTFChars(libDirJStr, libDirCStr);

    LOGI("Native library dir: %s", libDir.c_str());
    return libDir;
}




jstring injectPolicies(JNIEnv *env) {
    const char* sepolicy_bin = "/data/data/com.recorder.voip/files/sepolicy-inject";

    const char* policies[] = {
            "/data/local/tmp/sepolicy-inject -s audioserver -t apk_data_file -c dir -p search -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t shell_data_file -c dir -p search -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t unlabeled -c dir -p search,write,add_name,create,remove_name -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t unlabeled -c file -p open,read,write,getattr,map,execute,create,append,rename -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t apk_data_file -c file -p open,read,write,getattr,map,execute -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t audioserver -c process -p execmem -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t shell_exec -c file -p execute,execute_no_trans,read,open,map,getattr -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t system_file -c file -p execute,execute_no_trans,read,open,map,getattr -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t audioserver -c process -p setexec -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t shell -c process -p transition,noatsecure,siginh,rlimitinh -l",
            "/data/local/tmp/sepolicy-inject -s shell -t audioserver -c fifo_file -p write -l",
            "/data/local/tmp/sepolicy-inject -s system_server -t audioserver -c fifo_file -p write -l",
            "/data/local/tmp/sepolicy-inject -s audioserver -t toolbox_exec -c file -p execute,execute_no_trans,read,open,map,getattr -l",
            "/data/local/tmp/sepolicy-inject -s shell -t system_file -c file -p entrypoint -l"
    };

    // Loop over each policy and run the injection
    for (const char* policy : policies) {
        std::string command = std::string(sepolicy_bin) + " " + policy;
        std::string output = run_with_su(command.c_str());
        LOGI("Output: %s", output.c_str());
    }
    return env->NewStringUTF("Policies Injected");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_injectPolicies(JNIEnv *env, jobject thiz) {
    return injectPolicies(env);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_startMonitoring(JNIEnv *env, jobject thiz) {
    LOGI("startMonitoring");

//    LOGI("Disabling SELINUX");
//    const char* disableSelinux = "setenforce 0";
//    LOGI("%s", run_with_su(disableSelinux).c_str());

    injectPolicies(env);

    LOGI("Creating Directory /data/local/tmp/voip and changing context and permission");
    const char* dirctoryCreate = "mkdir -p /data/local/tmp/voip && chcon u:r:audioserver_data_file:s0 /data/local/tmp/voip && chmod 777 /data/local/tmp/voip && chown audioserver:audioserver /data/local/tmp/voip";
    LOGI("%s", run_with_su(dirctoryCreate).c_str());


    const char* path = "/data/data/com.recorder.voip/files/AndKittyInjector";
    const char* elf_path = env->GetStringUTFChars(env->NewStringUTF(path), 0);

    std::string libDir = getNativeLibDir(env, thiz);
    std::string fullPath = libDir + "/libaudiohook.so"; // or your ELF
    const char* hookpath = fullPath.c_str();

    std::string command = std::string(elf_path) +" -pkg /system/bin/audioserver -lib " + hookpath;

    std::string output = run_with_su(command.c_str());
    LOGI("%s", output.c_str());

    env->ReleaseStringUTFChars(env->NewStringUTF(path), elf_path);

    if (output.find("Injection succeeded") != std::string::npos) {
        return env->NewStringUTF("Monitoring Started.");
    } else {
        return env->NewStringUTF("Monitoring Start Failed. Disable Selinux or Check Logs...");
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_stopMonitoring(JNIEnv *env, jobject thiz) {
    LOGI("stopMonitoring");
    std::string command = "stop audioserver && start audioserver";
    std::string output = run_with_su(command.c_str());
    LOGI("%s", output.c_str());

    return env->NewStringUTF("Monitoring Stoppped.");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_moveDataToSdcard(JNIEnv *env, jobject thiz) {
    std::string command = "cp -r /data/local/tmp/voip /sdcard/ && chmod -R 777 /sdcard/voip && rm -rf /data/local/tmp/voip";
    std::string output = run_with_su(command.c_str());
    LOGI("%s", output.c_str());
    return env->NewStringUTF("Copied data to /sdcard and Deleted from /data/misc/audioserver/");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_deleteData(JNIEnv *env, jobject thiz) {
    std::string command = "rm -rf /sdcard/voip && rm -rf /data/local/tmp/voip";
    std::string output = run_with_su(command.c_str());
    LOGI("%s", output.c_str());
    return env->NewStringUTF("Data Deleted from /data/local/tmp/voip and /sdcard/voip/.");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_enableSelinux(JNIEnv *env, jobject thiz) {
    LOGI("Enabling SELINUX");
    const char* enableSelinux = "setenforce 1";
    std::string output = run_with_su(enableSelinux);
    LOGI("%s", output.c_str());

    return env->NewStringUTF("SELINUX Enforced.");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_recorder_voip_MainActivity_disableSelinux(JNIEnv *env, jobject thiz) {
    LOGI("Disabling SELINUX");
    const char* disableSelinux = "setenforce 0";
    std::string output = run_with_su(disableSelinux);
    LOGI("%s", output.c_str());

    return env->NewStringUTF("SELINUX Permissive.");
}
