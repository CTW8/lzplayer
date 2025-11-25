#ifndef __VE_VIDEO_RENDER__
#define __VE_VIDEO_RENDER__

#include <memory>
#include "platform/VEPlatform.h"
#include "interface/IVideoRender.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"

#if VE_PLATFORM_ANDROID
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window_jni.h>
#include "VEJvmOnLoad.h"
#endif

#include "VEVideoDecoder.h"
#include <string>
#include <iostream>
#include "VEAVsync.h"

namespace VE {
    class VEPlayer;

    class VEVideoRender : public AHandler {
    public:
        VEVideoRender(const std::shared_ptr<AMessage> &notify,
                      const std::shared_ptr<VEAVsync> &avSync);

        ~VEVideoRender();

        VEResult prepare(std::shared_ptr<VEVideoDecoder> decoder, VENativeWindow win, int width, int height, int fps);

        VEResult prepare(VEBundle params);

        VEResult start();

        VEResult pause();

        VEResult stop();

        VEResult release();

        VEResult setSurface(VENativeWindow win, int width, int height);

        VEResult seekTo(double timestamp);

        VEResult flush();

        VEResult setSpeedRate(double speed);

        enum {
            kWhatEOS = 'veos',
            kWhatProgress = 'prog'
        };

    private:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        VEResult onPrepare(VENativeWindow win);

        VEResult onStart();

        VEResult onStop();

        VEResult onPause();

        VEResult onRelease();

        VEResult onAVSync();

        VEResult onRender(std::shared_ptr<AMessage> msg);

        VEResult onSurfaceChanged(std::shared_ptr<AMessage> msg);

        VEResult onSetSpeedRate(double speed);

#if VE_PLATFORM_ANDROID
        GLuint loadShader(GLenum type, const char *shaderSrc);

        GLuint createProgram(const char *vertexSource, const char *fragmentSource);

        bool createTexture();
#endif

        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatSpeedRate = 'rate',
            kWhatSync = 'sync',
            kWhatRender = 'rend',
            kWhatRelease = 'rele',
            kWhatPause = 'paus',
            kWhatSurfaceChanged = 'surf'
        };

    private:
        VENativeWindow mWin = nullptr;
        bool mIsStarted = false;

        std::shared_ptr<VEVideoDecoder> mVDec = nullptr;
        std::shared_ptr<AMessage> mNotify = nullptr;

#if VE_PLATFORM_ANDROID
        GLuint mTextures[3]{};
        GLuint mProgram{};
        GLuint mVAO{}, mVBO{};
        EGLDisplay eglDisplay = EGL_NO_DISPLAY;
        EGLSurface eglSurface = EGL_NO_SURFACE;
        EGLContext eglContext = EGL_NO_CONTEXT;
        EGLConfig eglConfig = nullptr;
#endif

        int mViewWidth = 0;
        int mViewHeight = 0;

        double mSpeedRate = 1.0f;

        int mFrameWidth = 0;
        int mFrameHeight = 0;

        FILE *fp = nullptr;

        std::shared_ptr<VEAVsync> m_AVSync;
    };
}
#endif