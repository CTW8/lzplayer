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
class VEPlayer;
class VEVideoRender:public AHandler
{
public:
    VEVideoRender(std::shared_ptr<AMessage> notify);
    ~VEVideoRender();

    status_t init(std::shared_ptr<VEVideoDecoder> decoder, ANativeWindow *win, int width, int height, int fps);
    status_t start();
    status_t pause();
    status_t resume();
    status_t stop();
    status_t unInit();
    enum {
        kWhatEOS            = 'eosf',
        kWhatProgress       = 'prog'
    };

private:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    status_t onInit(ANativeWindow * win);
    status_t onStart();
    status_t onStop();
    status_t onPause();
    status_t onReume();
    status_t onUnInit();
    status_t onAVSync();
    status_t onRender(std::shared_ptr<AMessage> msg);

    GLuint loadShader(GLenum type, const char *shaderSrc);

    GLuint createProgram(const char *vertexSource, const char *fragmentSource);
    bool createTexture();

    enum {
        kWhatInit                = 'init',
        kWhatStart               = 'star',
        kWhatStop                = 'stop',
        kWhatSync                = 'sync',
        kWhatRender              = 'rend',
        kWhatUninit              = 'unin'
    };

private:
    ANativeWindow *mWin = nullptr;
    bool           mIsStarted = false;
    std::shared_ptr<VEVideoDecoder> mVDec = nullptr;
    std::shared_ptr<AMessage> mNotify = nullptr;

    GLuint          mTextures[3]{};
    GLuint          mProgram{};
    GLuint  mVAO{},mVBO{};
    EGLDisplay eglDisplay{};
    EGLSurface eglSurface{};
    EGLContext eglContext{};

    int mViewWidth = 0;
    int mViewHeight = 0;

    int mFrameWidth = 0;
    int mFrameHeight = 0;

    FILE *fp = nullptr;
};

#endif