//
// Created by 李振 on 2024/6/11.
//

#include "VEJvmOnLoad.h"
#include <jni.h>
JavaVM* gJvm = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        ALOGE("Failed to get the environment using GetEnv()");
        return JNI_ERR;
    }

    // Register native methods here if needed
    // jclass clazz = env->FindClass("com/example/myapp/MainActivity");
    // if (clazz == nullptr) {
    //     ALOGE("Failed to find class 'com/example/myapp/MainActivity'");
    //     return JNI_ERR;
    // }
    // JNINativeMethod methods[] = {
    //     {"nativeMethod", "()V", reinterpret_cast<void*>(Java_com_example_myapp_MainActivity_nativeMethod)}
    // };
    // if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) < 0) {
    //     ALOGE("Failed to register native methods");
    //     return JNI_ERR;
    // }

    gJvm = vm;
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
