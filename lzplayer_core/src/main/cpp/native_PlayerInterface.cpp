//
// Created by 李振 on 2024/7/25.
//

#include <android/native_window_jni.h>
#include "native_PlayerInterface.h"
#include "utils/Log.h"
#include "VEPlayerDirver.h"
#include "ScopedUtfChars.h"
#include "VEJvmOnLoad.h"
#include "JNIHelpers.h"

#define  CHECK_NULL()  {if(vePlayer == nullptr){ return -1;}}while(0)
//-----------------------------------------------------------------------------------
// ref-counted object for callbacks
class JNIMediaPlayerListener: public MediaPlayerListener
{
public:
    JNIMediaPlayerListener(JNIEnv* env, jobject thiz, jobject weak_thiz);
    ~JNIMediaPlayerListener();
    virtual void notify(int msg, int ext1, int ext2, const void *obj = NULL);
private:
    JNIMediaPlayerListener();
    jclass      mClass;     // Reference to MediaPlayer class
    jobject     mObject;    // Weak ref to MediaPlayer Java object to call on
};

JNIMediaPlayerListener::JNIMediaPlayerListener(JNIEnv* env, jobject thiz, jobject weak_thiz)
{

    // Hold onto the MediaPlayer class for use in calling the static method
    // that posts events to the application thread.
    jclass clazz = env->GetObjectClass(thiz);
    if (clazz == NULL) {
        ALOGE("Can't find android/media/MediaPlayer");
        jniThrowException(env, "java/lang/Exception", NULL);
        return;
    }
    mClass = (jclass)env->NewGlobalRef(clazz);

    // We use a weak reference so the MediaPlayer object can be garbage collected.
    // The reference is only used as a proxy for callbacks.
    mObject  = env->NewGlobalRef(weak_thiz);
}

JNIMediaPlayerListener::~JNIMediaPlayerListener()
{
    // remove global references
    JNIEnv *env = AttachCurrentThreadEnv();
    env->DeleteGlobalRef(mObject);
    env->DeleteGlobalRef(mClass);
}

void JNIMediaPlayerListener::notify(int msg, int ext1, int ext2, const void *obj)
{
    JNIEnv *env = AttachCurrentThreadEnv();
    switch (msg) {
        default:
            break;
    }
}
//-----------------------------------------------------------------------------------

jlong createNativeHandle(JNIEnv *env, jclass clazz) {
    ALOGD("createNativeHandle called");
    VEPlayerDirver *player = new VEPlayerDirver();


    jclass clazzNativeLib;

    clazzNativeLib = env->FindClass("com/example/lzplayer_core/NativeLib");
    if (clazzNativeLib == NULL) {
        return -1;
    }

    fields.post_event = env->GetStaticMethodID(clazz, "postEventFromNative",
                                               "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    if (fields.post_event == NULL) {
        return;
    }

    env->DeleteLocalRef(clazzNativeLib);

    return reinterpret_cast<jlong>(player);
}

// 初始化
jint nativeInit(JNIEnv *env, jobject obj, jlong handle, jstring path) {
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    const char *nativePath = ScopedUtfChars(env,path).c_str();
    return vePlayer->setDataSource(nativePath);
}

// 设置 Surface
jint nativeSetSurface(JNIEnv *env, jobject obj, jlong handle, jobject surface, jint width, jint height) {
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();

    ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
    ALOGD("nativeSetSurface called with handle: %ld, width: %d, height: %d", handle, width, height);

    return vePlayer->setDisplayOut(nativeWindow,width,height);
}

// 获取时长
jlong nativeGetDuration(JNIEnv *env, jobject obj, jlong handle) {
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    ALOGD("nativeGetDuration called with handle: %ld", handle);

    return vePlayer->getDuration();
}

// 开始播放
jint nativeStart(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("nativeStart called with handle: %ld", handle);
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();

    return vePlayer->start();
}

// 暂停播放
jint nativePause(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("nativePause called with handle: %ld", handle);
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    return vePlayer->pause();
}

// 停止播放
jint nativeStop(JNIEnv *env, jobject obj, jlong handle) {
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    ALOGD("nativeStop called with handle: %ld", handle);
    return vePlayer->stop();
}

// 跳转
jint nativeSeekTo(JNIEnv *env, jobject obj, jlong handle, jlong timestamp) {
    ALOGD("nativeSeekTo called with handle: %ld, timestamp: %ld", handle, timestamp);
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    return vePlayer->seek(timestamp);
}

// 释放资源
jint nativeRelease(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("nativeRelease called with handle: %ld", handle);
    VEPlayer * vePlayer = reinterpret_cast<VEPlayer*>(handle);
    CHECK_NULL();
    delete vePlayer;
    return 0;
}
