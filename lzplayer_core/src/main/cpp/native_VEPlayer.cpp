#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include "VEPlayer.h"
#include "JniString.h"

#define  CHECK_NULL()  {if(vePlayer == nullptr){ return -1;}}while(0)
extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_lzplayer_1core_NativeLib_createNativeHandle(JNIEnv *env, jclass clazz) {
    // TODO: implement createNativeHandle()
    return reinterpret_cast<long>(new VEPlayer());
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeInit(JNIEnv *env, jobject thiz, jlong handle,
                                                     jstring path, jobject surface) {
    // TODO: implement nativeInit()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    JniString jPath(env,path);
    vePlayer->setDataSource(jPath.c_str());
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    vePlayer->setDisplayOut(window);
    vePlayer->prepare();
    CHECK_NULL();
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeSetSurface(JNIEnv *env, jobject thiz, jlong handle,
                                                           jobject surface) {
    // TODO: implement nativeSetSurface()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeStart(JNIEnv *env, jobject thiz, jlong handle) {
    // TODO: implement nativeStart()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    vePlayer->start();
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativePause(JNIEnv *env, jobject thiz, jlong handle) {
    // TODO: implement nativePause()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    vePlayer->pause();
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeStop(JNIEnv *env, jobject thiz, jlong handle) {
    // TODO: implement nativeStop()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    vePlayer->stop();
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeSeekTo(JNIEnv *env, jobject thiz, jlong handle,
                                                       jlong timestamp) {
    // TODO: implement nativeSeekTo()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    vePlayer->seek(timestamp);
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeRelease(JNIEnv *env, jobject thiz, jlong handle) {
    // TODO: implement nativeRelease()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    vePlayer->release();
    return 0;
}