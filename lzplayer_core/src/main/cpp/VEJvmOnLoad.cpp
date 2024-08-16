//
// Created by 李振 on 2024/6/11.
//

#include "VEJvmOnLoad.h"
#include <jni.h>

#include "native_PlayerInterface.h"
JavaVM* gJvm = nullptr;


// 定义 JNI 方法表
static JNINativeMethod gVEPlayerMethods[] = {
        {"createNativeHandle", "()J", (void *)createNativeHandle},
        {"nativeInit", "(Ljava/lang/Object;JLjava/lang/String;)I", (void *)nativeInit},
        {"nativeSetSurface", "(JLandroid/view/Surface;II)I", (void *)nativeSetSurface},
        {"nativeGetDuration", "(J)J", (void *)nativeGetDuration},
        {"nativePrepare", "(J)I", (void *)nativePrepare},
        {"nativePrepareAsync", "(J)I", (void *)nativePrepareAsync},
        {"nativeStart", "(J)I", (void *)nativeStart},
        {"nativePause", "(J)I", (void *)nativePause},
        {"nativeStop", "(J)I", (void *)nativeStop},
        {"nativeSeekTo", "(JJ)I", (void *)nativeSeekTo},
        {"nativeRelease", "(J)I", (void *)nativeRelease},
};


JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
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
    if (env->RegisterNatives(clazz, gVEPlayerMethods, sizeof(gVEPlayerMethods) / sizeof(gVEPlayerMethods[0])) < 0) {
        return -1;
    }

    ALOGI("JNI_OnLoad called successfully");
    return JNI_VERSION_1_6;
}


JNIEnv * AttachCurrentThreadEnv(){
    return AttachCurrentThreadEnvWithName(nullptr);
}

JNIEnv *AttachCurrentThreadEnvWithName(const char *threadName){
    JNIEnv *env= nullptr;
    gJvm->GetEnv((void**)&env,JNI_VERSION_1_6);
    if(env!= nullptr){
        return env;
    }

    char name[32] ={0};

    if(threadName == nullptr || threadName[0] == '\0'){
        prctl(PR_GET_NAME,name);
        threadName = name;
    }

    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_6;
    args.name = threadName;
    args.group = nullptr;
    gJvm->AttachCurrentThread(&env,&args);
    return env;
}
