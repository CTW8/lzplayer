//
// Created by 李振 on 2024/7/25.
//

#ifndef LZPLAYER_NATIVE_PLAYERINTERFACE_H
#define LZPLAYER_NATIVE_PLAYERINTERFACE_H

#include <jni.h>


#ifdef __cplusplus
extern "C" {
#endif

jlong createNativeHandle(JNIEnv *env, jclass clazz);
jint nativeInit(JNIEnv *env, jobject thiz,jobject weak_this, jlong handle, jstring path);
jint nativeSetSurface(JNIEnv *env, jobject obj, jlong handle, jobject surface, jint width, jint height);
jlong nativeGetDuration(JNIEnv *env, jobject obj, jlong handle);
jint nativeStart(JNIEnv *env, jobject obj, jlong handle);
jint nativePause(JNIEnv *env, jobject obj, jlong handle);
jint nativeStop(JNIEnv *env, jobject obj, jlong handle);
jint nativeSeekTo(JNIEnv *env, jobject obj, jlong handle, jlong timestamp);
jint nativeRelease(JNIEnv *env, jobject obj, jlong handle);

#ifdef __cplusplus
}
#endif

#endif //LZPLAYER_NATIVE_PLAYERINTERFACE_H
