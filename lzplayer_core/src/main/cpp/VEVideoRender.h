#ifndef __VE_VIDEO_RENDER__
#define __VE_VIDEO_RENDER__
#include <memory>
#include "AHandler.h"
#include "AMessage.h"
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window_jni.h>
#include "VEJvmOnLoad.h"
#include "VEVideoDecoder.h"
#include <string>
#include <iostream>

class VEVideoRender:public AHandler
{
public:
    VEVideoRender();
    ~VEVideoRender();

    status_t init(std::shared_ptr<VEVideoDecoder> decoder,ANativeWindow *win);
    status_t start();
    status_t stop();
    status_t unInit();

private:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    bool onInit(ANativeWindow * win);
    bool onStart();
    bool onStop();
    bool onUnInit();
    bool onRender();

    bool createPragram();
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


    GLuint          mTexture[3];
    GLuint          mProgram;
    GLuint  mVAO,mVBO;
};

#endif