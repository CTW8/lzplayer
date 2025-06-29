//
// Created by 李振 on 2024/6/11.
//

#include "VEJvmOnLoad.h"
#include <jni.h>

#include "native_PlayerInterface.h"
    JavaVM *gJvm = nullptr;


// 定义 JNI 方法表
static JNINativeMethod gVEPlayerMethods[] = {
        {"createNativeHandle", "()J",                                      (void *) VE::createNativeHandle},
        {"nativeInit",         "(Ljava/lang/Object;JLjava/lang/String;)I", (void *) VE::nativeInit},
        {"nativeSetSurface",   "(JLandroid/view/Surface;II)I",             (void *) VE::nativeSetSurface},
        {"nativeGetDuration",  "(J)J",                                     (void *) VE::nativeGetDuration},
        {"nativePrepare",      "(J)I",                                     (void *) VE::nativePrepare},
        {"nativePrepareAsync", "(J)I",                                     (void *) VE::nativePrepareAsync},
        {"nativeStart",        "(J)I",                                     (void *) VE::nativeStart},
        {"nativePause",        "(J)I",                                     (void *) VE::nativePause},
        {"nativeResume",       "(J)I",                                     (void *) VE::nativeResume},
        {"nativeStop",         "(J)I",                                     (void *) VE::nativeStop},
        {"nativeSeekTo",       "(JD)I",                                    (void *) VE::nativeSeekTo},
        {"nativeRelease",      "(J)I",                                     (void *) VE::nativeRelease},
        {"setLooping",         "(JZ)I",                                    (void *) VE::nativeSetLooping},
        {"setPlaySpeed",       "(JF)I",                                    (void *) VE::nativeSetPlaySpeed},
};


JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        ALOGE("Failed to get the environment using GetEnv()");
        return JNI_ERR;
    }

    gJvm = vm;

    // 获取 Java 类
    jclass clazz = env->FindClass("com/example/lzplayer_core/NativeLib");
    if (clazz == nullptr) {
        return -1;
    }

    // 注册 JNI 方法
    if (env->RegisterNatives(clazz, gVEPlayerMethods,
                             sizeof(gVEPlayerMethods) / sizeof(gVEPlayerMethods[0])) < 0) {
        return -1;
    }

    ALOGI("JNI_OnLoad called successfully");
    return JNI_VERSION_1_6;
}


JNIEnv *AttachCurrentThreadEnv() {
    return AttachCurrentThreadEnvWithName(nullptr);
}

JNIEnv *AttachCurrentThreadEnvWithName(const char *threadName) {
    JNIEnv *env = nullptr;
    gJvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (env != nullptr) {
        return env;
    }

    char name[32] = {0};

    if (threadName == nullptr || threadName[0] == '\0') {
        prctl(PR_GET_NAME, name);
        threadName = name;
    }

    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_6;
    args.name = threadName;
    args.group = nullptr;
    gJvm->AttachCurrentThread(&env, &args);
    return env;
}
