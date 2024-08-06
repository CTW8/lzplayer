#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include "VEPlayer.h"
#include "JniString.h"

#define  CHECK_NULL()  {if(vePlayer == nullptr){ return -1;}}while(0)


static jobject global_NativeLib;

extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_lzplayer_1core_NativeLib_createNativeHandle(JNIEnv *env, jclass clazz) {
    // TODO: implement createNativeHandle()
    return reinterpret_cast<long>(new VEPlayer());
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeInit(JNIEnv *env, jobject thiz, jlong handle,
                                                     jstring path) {
    global_NativeLib = env->NewGlobalRef(thiz);
    // TODO: implement nativeInit()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    vePlayer->setOnInfoListener([](int type,int msg1,double msg2,std::string msg3,void*msg4){
        JNIEnv *env = AttachCurrentThreadEnv();
        jclass jclass_NativeLib = env->GetObjectClass(global_NativeLib);
        jmethodID methodId = env->GetMethodID(jclass_NativeLib, "onNativeInfoCallback", "(IIDLjava/lang/String;Ljava/lang/Object;)V");

        jstring message = env->NewStringUTF(msg3.c_str());
        jobject someObject = NULL; // 或者用一个实际的Java对象
        env->CallVoidMethod(global_NativeLib, methodId, type, msg1, msg2, message, someObject);
        env->DeleteLocalRef(message);
        env->DeleteLocalRef(jclass_NativeLib);
    });

    vePlayer->setOnProgressListener([](int progress){
        JNIEnv *env = AttachCurrentThreadEnv();
        jclass jclass_NativeLib = env->GetObjectClass(global_NativeLib);
        jmethodID methodId = env->GetMethodID(jclass_NativeLib, "onNativeProgress", "(I)V");

        env->CallVoidMethod(global_NativeLib, methodId, progress);
        env->DeleteLocalRef(jclass_NativeLib);
    });

    JniString jPath(env,path);
    vePlayer->setDataSource(jPath.c_str());
    vePlayer->prepare();
    CHECK_NULL();
    return 0;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeSetSurface(JNIEnv *env, jobject thiz, jlong handle,
                                                           jobject surface,jint width,jint height) {
    // TODO: implement nativeSetSurface()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    vePlayer->setDisplayOut(window,width,height);
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
    env->DeleteGlobalRef(global_NativeLib);
    return 0;
}
extern "C"
JNIEXPORT jlong JNICALL
Java_com_example_lzplayer_1core_NativeLib_nativeGetDuration(JNIEnv *env, jobject thiz,
                                                            jlong handle) {
    // TODO: implement nativeGetDuration()
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    return vePlayer->getDuration();
}