//
// Created by 李振 on 2024/6/11.
//

#include "VEJvmOnLoad.h"
#include <jni.h>
#include <pthread.h>

#include "native_PlayerInterface.h"
    JavaVM *gJvm = nullptr;

namespace {
    // attach 过 JVM 的 native 线程(各 ALooper 线程)退出前必须 detach，
    // 否则 ART 直接 FATAL abort 进程。用 pthread_key 的析构回调在线程
    // 退出时自动补上，只对经由 AttachCurrentThreadEnv 附加的线程生效。
    pthread_key_t gDetachKey;
    pthread_once_t gDetachKeyOnce = PTHREAD_ONCE_INIT;

    void detachCurrentThread(void *) {
        if (gJvm != nullptr) {
            gJvm->DetachCurrentThread();
        }
    }

    void makeDetachKey() {
        pthread_key_create(&gDetachKey, detachCurrentThread);
    }
}


// 定义 JNI 方法表
static JNINativeMethod gVEPlayerMethods[] = {
        {"createNativeHandle", "()J",                                      (void *) VE::createNativeHandle},
        {"nativeInit",         "(Ljava/lang/Object;JLjava/lang/String;)I", (void *) VE::nativeInit},
        {"nativeSetSurface",   "(JLandroid/view/Surface;II)I",             (void *) VE::nativeSetSurface},
        {"nativeGetDuration",  "(J)J",                                     (void *) VE::nativeGetDuration},
        {"nativeGetCurrentPosition", "(J)J",                               (void *) VE::nativeGetCurrentPosition},
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
        {"nativeGetTrackInfo", "(J)Ljava/lang/String;",                    (void *) VE::nativeGetTrackInfo},
        {"nativeSelectTrack",  "(JI)I",                                    (void *) VE::nativeSelectTrack},
        {"nativeDeselectTrack","(JI)I",                                    (void *) VE::nativeDeselectTrack},
        {"nativeAddExternalSubtitle", "(JLjava/lang/String;)I",            (void *) VE::nativeAddExternalSubtitle},
        {"nativeGetStats",     "(J)Ljava/lang/String;",                    (void *) VE::nativeGetStats},
        {"nativeSetForceSoftwareDecoder", "(JZ)I",                         (void *) VE::nativeSetForceSoftwareDecoder},
        {"nativeSetForceSlesAudio", "(JZ)I",                               (void *) VE::nativeSetForceSlesAudio},
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
    if (gJvm->AttachCurrentThread(&env, &args) != JNI_OK) {
        return nullptr;
    }

    // 打上标记，线程退出时由 key 的析构回调自动 DetachCurrentThread
    pthread_once(&gDetachKeyOnce, makeDetachKey);
    pthread_setspecific(gDetachKey, reinterpret_cast<void *>(1));
    return env;
}
