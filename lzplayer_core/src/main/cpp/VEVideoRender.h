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
    VEVideoRender();
    ~VEVideoRender();

    status_t init(std::shared_ptr<VEVideoDecoder> decoder, ANativeWindow *win, int width, int height, int fps,
                  VEPlayer* player);
    status_t start();
    status_t pause();
    status_t resume();
    status_t stop();
    status_t unInit();

private:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    bool onInit(ANativeWindow * win);
    bool onStart();
    bool onStop();
    bool onPause();
    bool onReume();
    bool onUnInit();
    bool onRender();

    GLuint loadShader(GLenum type, const char *shaderSrc);

    GLuint createProgram(const char *vertexSource, const char *fragmentSource);
    bool createTexture();

    enum {
        kWhatInit                = 'init',
        kWhatStart               = 'star',
        kWhatStop                = 'stop',
        kWhatRender              = 'rend',
        kWhatUninit              = 'unin'
    };

private:
    ANativeWindow *mWin = nullptr;
    bool           mIsStarted = false;
    std::shared_ptr<VEVideoDecoder> mVDec = nullptr;
    VEPlayer* mPlayer = nullptr;

    GLuint          mTextures[3];
    GLuint          mProgram;
    GLuint  mVAO,mVBO;
    EGLDisplay eglDisplay;
    EGLSurface eglSurface;
    EGLContext eglContext;

    int mViewWidth = 0;
    int mViewHeight = 0;

    int mFrameWidth = 0;
    int mFrameHeight = 0;

    FILE *fp = nullptr;
};

#endif