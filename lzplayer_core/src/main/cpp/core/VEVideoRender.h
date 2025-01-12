#ifndef __VE_VIDEO_RENDER__
#define __VE_VIDEO_RENDER__

#include <memory>
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window_jni.h>
#include "VEJvmOnLoad.h"
#include "VEVideoDecoder.h"
#include <string>
#include <iostream>
#include "VEAVsync.h"

class VEPlayer;
class VEVideoRender:public AHandler
{
public:
    VEVideoRender(std::shared_ptr<AMessage> notify, std::shared_ptr<VEAVsync> avSync);
    ~VEVideoRender();

    VEResult init(std::shared_ptr<VEVideoDecoder> decoder, ANativeWindow *win, int width, int height, int fps);
    VEResult start();
    VEResult pause();
    VEResult resume();
    VEResult stop();
    VEResult unInit();
    VEResult setSurface(ANativeWindow *win, int width, int height);

    enum {
        kWhatEOS            = 'veos',
        kWhatProgress       = 'prog'
    };

private:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    VEResult onInit(ANativeWindow * win);
    VEResult onStart();
    VEResult onStop();
    VEResult onPause();
    VEResult onResume();
    VEResult onUnInit();
    VEResult onAVSync();
    VEResult onRender(std::shared_ptr<AMessage> msg);
    VEResult onSurfaceChanged(std::shared_ptr<AMessage> msg);

    GLuint loadShader(GLenum type, const char *shaderSrc);

    GLuint createProgram(const char *vertexSource, const char *fragmentSource);
    bool createTexture();

    enum {
        kWhatInit                = 'init',
        kWhatStart               = 'star',
        kWhatStop                = 'stop',
        kWhatSync                = 'sync',
        kWhatRender              = 'rend',
        kWhatUninit              = 'unin',
        kWhatPause               = 'paus',
        kWhatResume              = 'resm',
        kWhatSurfaceChanged      = 'surf'
    };

private:
    ANativeWindow *mWin = nullptr;
    bool           mIsStarted = false;

    std::shared_ptr<VEVideoDecoder> mVDec = nullptr;
    std::shared_ptr<AMessage> mNotify = nullptr;

    GLuint          mTextures[3]{};
    GLuint          mProgram{};
    GLuint  mVAO{},mVBO{};
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLConfig eglConfig = nullptr;

    int mViewWidth = 0;
    int mViewHeight = 0;

    int mFrameWidth = 0;
    int mFrameHeight = 0;

    FILE *fp = nullptr;

    std::shared_ptr<VEAVsync> m_AVSync;
};

#endif