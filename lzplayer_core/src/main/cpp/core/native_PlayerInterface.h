//
// Created by 李振 on 2024/7/25.
//

#ifndef LZPLAYER_NATIVE_PLAYERINTERFACE_H
#define LZPLAYER_NATIVE_PLAYERINTERFACE_H

#include <jni.h>

namespace VE {
#ifdef __cplusplus
    extern "C" {
#endif

    jlong createNativeHandle(JNIEnv *env, jclass clazz);
    jint nativeInit(JNIEnv *env, jobject thiz, jobject weak_this, jlong handle, jstring path);
    jint nativeSetSurface(JNIEnv *env, jobject obj, jlong handle, jobject surface, jint width,
                          jint height);
    jlong nativeGetDuration(JNIEnv *env, jobject obj, jlong handle);
    jlong nativeGetCurrentPosition(JNIEnv *env, jobject obj, jlong handle);
    jint nativePrepare(JNIEnv *env, jobject obj, jlong handle);
    jint nativePrepareAsync(JNIEnv *env, jobject obj, jlong handle);
    jint nativeStart(JNIEnv *env, jobject obj, jlong handle);
    jint nativePause(JNIEnv *env, jobject obj, jlong handle);
    jint nativeResume(JNIEnv *env, jobject obj, jlong handle);
    jint nativeStop(JNIEnv *env, jobject obj, jlong handle);
    jint nativeSeekTo(JNIEnv *env, jobject obj, jlong handle, jdouble timestamp);
    jint nativeRelease(JNIEnv *env, jobject obj, jlong handle);

// 新增的本地方法声明
    jint nativeSetLooping(JNIEnv *env, jobject obj, jlong handle, jboolean loop);
    jint nativeSetPlaySpeed(JNIEnv *env, jobject obj, jlong handle, jfloat speed);

    jstring nativeGetTrackInfo(JNIEnv *env, jobject obj, jlong handle);

    jint nativeSelectTrack(JNIEnv *env, jobject obj, jlong handle, jint trackIndex);

    jint nativeDeselectTrack(JNIEnv *env, jobject obj, jlong handle, jint trackIndex);

    jint nativeAddExternalSubtitle(JNIEnv *env, jobject obj, jlong handle, jstring path);

    jstring nativeGetStats(JNIEnv *env, jobject obj, jlong handle);
    jstring nativeGetStartupTrace(JNIEnv *env, jobject obj, jlong handle);
    jstring nativeGetSeekTrace(JNIEnv *env, jobject obj, jlong handle);

    jint nativeSetFaultInject(JNIEnv *env, jobject obj, jboolean failCreate,
                              jboolean failConfigure, jint failAfterFrames);
    jint nativeSetForceSoftwareDecoder(JNIEnv *env, jobject obj, jlong handle, jboolean force);

    jint nativeSetForceSlesAudio(JNIEnv *env, jobject obj, jlong handle, jboolean force);
    jint nativeSetPreferVulkanRender(JNIEnv *env, jobject obj, jlong handle, jboolean prefer);

#ifdef __cplusplus
    }
#endif
}
#endif //LZPLAYER_NATIVE_PLAYERINTERFACE_H
