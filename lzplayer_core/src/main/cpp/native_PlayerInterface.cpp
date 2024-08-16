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
#include "VEDef.h"

#define  CHECK_NULL()  {if(vePlayer == nullptr){ return -1;}}while(0)

jmethodID jNativeCallback;
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
        case VE_PLAYER_NOTIFY_EVENT_ON_EOS:{
            env->CallStaticVoidMethod(mClass, jNativeCallback, mObject,msg, ext1, ext2, NULL);
            break;
        }
        case VE_PLAYER_NOTIFY_EVENT_ON_ERROR:{
            env->CallStaticVoidMethod(mClass, jNativeCallback, mObject,msg, ext1, ext2, NULL);
            break;
        }
        case VE_PLAYER_NOTIFY_EVENT_ON_INFO:{
            env->CallStaticVoidMethod(mClass, jNativeCallback, mObject,msg, ext1, ext2, NULL);
            break;
        }
        case VE_PLAYER_NOTIFY_EVENT_ON_PREPARED:{
            env->CallStaticVoidMethod(mClass, jNativeCallback, mObject,msg, ext1, ext2, NULL);
            break;
        }
        case VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS:{
            env->CallStaticVoidMethod(mClass, jNativeCallback, mObject,msg, ext1, ext2, NULL);
            break;
        }
        default:
            break;
    }
}
//-----------------------------------------------------------------------------------

jlong createNativeHandle(JNIEnv *env, jclass clazz) {
    ALOGD("%s %d called",__FUNCTION__ ,__LINE__);
    VEPlayerDirver *player = new VEPlayerDirver();


    jclass clazzNativeLib;

    clazzNativeLib = env->FindClass("com/example/lzplayer_core/NativeLib");
    if (clazzNativeLib == NULL) {
        return -1;
    }

    jNativeCallback = env->GetStaticMethodID(clazz, "postEventFromNative",
                                               "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    if (jNativeCallback == NULL) {
        return -1;
    }

    env->DeleteLocalRef(clazzNativeLib);

    return reinterpret_cast<jlong>(player);
}

// 初始化
jint nativeInit(JNIEnv *env, jobject thiz,jobject weak_this, jlong handle, jstring path) {
    ALOGD("%s %d called",__FUNCTION__ ,__LINE__);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();
    std::shared_ptr<JNIMediaPlayerListener> listener = std::make_shared<JNIMediaPlayerListener>(env,thiz,weak_this);
    ScopedUtfChars tmp(env, path);
    std::string filePath = tmp.c_str();
    return vePlayer->setDataSource(filePath);
}

// 设置 Surface
jint nativeSetSurface(JNIEnv *env, jobject obj, jlong handle, jobject surface, jint width, jint height) {
    ALOGD("%s %d called",__FUNCTION__ ,__LINE__);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();

    ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
    ALOGD("nativeSetSurface called with handle: %ld, width: %d, height: %d", handle, width, height);
    {
        GLuint          mProgram;
        GLuint  mVAO,mVBO;
        EGLDisplay eglDisplay;
        EGLSurface eglSurface;
        EGLContext eglContext;

        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL_NO_DISPLAY) {
            ALOGE("VEVideoRender eglGetDisplay failed");
            return false;
        }

        // 初始化 EGL 显示设备
        EGLint major, minor;
        if (!eglInitialize(eglDisplay, &major, &minor)) {
            ALOGE("VEVideoRender eglInitialize failed");
            return false;
        }

        // 配置 EGL 表面属性
        EGLConfig config;
        EGLint numConfigs;
        EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_NONE
        };
        if (!eglChooseConfig(eglDisplay, configAttribs, &config, 1, &numConfigs)) {
            ALOGE("VEVideoRender eglChooseConfig failed");
            return false;
        }

        // 创建 EGL 上下文
        EGLint contextAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3, // OpenGL ES 3.0
                EGL_NONE
        };
        eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext == EGL_NO_CONTEXT) {
            ALOGE("VEVideoRender eglCreateContext failed");
            return false;
        }

        // 创建 EGL 表面
        eglSurface = eglCreateWindowSurface(eglDisplay, config, nativeWindow, NULL);

        // 将 EGL 上下文与当前线程关联
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            ALOGE("VEVideoRender eglMakeCurrent failed");
            return false;
        }

        ALOGD("VEVideoRender mViewWidth:%d,mViewHeight:%d",width,height);
        glViewport(0,0,width,height);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.5f, 1.0f, 1.0f, 1.0f);
    }
    return 0;//vePlayer->setSurface(nativeWindow,width,height);
}

// 获取时长
jlong nativeGetDuration(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("%s %d called",__FUNCTION__ ,__LINE__);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();
    ALOGD("nativeGetDuration called with handle: %ld", handle);

    return vePlayer->getDuration();
}

// 开始播放
jint nativeStart(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("nativeStart called with handle: %ld", handle);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();

    return vePlayer->start();
}

// 暂停播放
jint nativePause(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("nativePause called with handle: %ld", handle);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();
    return vePlayer->pause();
}

// 停止播放
jint nativeStop(JNIEnv *env, jobject obj, jlong handle) {
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();
    ALOGD("nativeStop called with handle: %ld", handle);
    return vePlayer->stop();
}

// 跳转
jint nativeSeekTo(JNIEnv *env, jobject obj, jlong handle, jlong timestamp) {
    ALOGD("nativeSeekTo called with handle: %ld, timestamp: %ld", handle, timestamp);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();
    return vePlayer->seekTo(timestamp);
}

// 释放资源
jint nativeRelease(JNIEnv *env, jobject obj, jlong handle) {
    ALOGD("nativeRelease called with handle: %ld", handle);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();
    delete vePlayer;
    return 0;
}

jint nativePrepare(JNIEnv *env, jobject obj, jlong handle)
{
    ALOGD("nativePrepare called with handle: %ld", handle);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();

    return vePlayer->prepare();
}
jint nativePrepareAsync(JNIEnv *env, jobject obj, jlong handle)
{
    ALOGD("nativePrepareAsync called with handle: %ld", handle);
    VEPlayerDirver * vePlayer = reinterpret_cast<VEPlayerDirver*>(handle);
    CHECK_NULL();

    return vePlayer->prepareAsync();
}
