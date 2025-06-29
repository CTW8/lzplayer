#include "VEVideoRender.h"
#include "VEJvmOnLoad.h"
#include "glm/fwd.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/ext/matrix_projection.hpp"
#include "VEPlayer.h"
#include "VEAVsync.h"

namespace VE {
    const char *vertexShaderSource = R"(
#version 300 es
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform mat4 u_TransformMatrix;
void main() {
    gl_Position = u_TransformMatrix * aPosition;
    vTexCoord = aTexCoord;
}
)";

    const char *fragmentShaderSource = R"(
#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
out vec4 fragColor;
void main() {
        float y = texture(yTexture, vTexCoord).r;
        float u = texture(uTexture, vTexCoord).r - 0.5;
        float v = texture(vTexture, vTexCoord).r - 0.5;
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        fragColor = vec4(r, g, b, 1.0);
}
)";

    void VEVideoRender::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        switch (msg->what()) {
            case kWhatInit: {
                msg->findPointer("win", (void **) &mWin);
                std::shared_ptr<void> tmp = nullptr;

                msg->findObject("vdec", &tmp);
                mVDec = std::static_pointer_cast<VEVideoDecoder>(tmp);

                msg->findInt32("width", &mViewWidth);
                msg->findInt32("height", &mViewHeight);
                onPrepare(mWin);
                break;
            }
            case kWhatStart: {
                onStart();
                break;
            }
            case kWhatSync: {
                if (onAVSync() != VE_OK) {
                    ALOGE("VEVideoRender::onAVSync failed");
                }
                break;
            }
            case kWhatRender: {
                if (onRender(msg) == VE_OK) {
                    std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatSync,
                                                                                     shared_from_this());
                    renderMsg->post();
                }
                break;
            }
            case kWhatStop: {
                onStop();
                break;
            }
            case kWhatRelease: {
                onRelease();
                break;
            }
            case kWhatPause: {
                mIsStarted = false;
                break;
            }
            case kWhatSurfaceChanged: {
                onSurfaceChanged(msg);
                break;
            }
            default: {
                ALOGW("VEVideoRender::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEVideoRender::VEVideoRender(const std::shared_ptr<AMessage> &notify,
                                 const std::shared_ptr<VEAVsync> &avSync)
            : mNotify(notify), m_AVSync(avSync) {
        ALOGD("VEVideoRender structure");
    }

    VEVideoRender::~VEVideoRender() {
        ALOGD("~VEVideoRender release");
        stop();
    }

    VEResult
    VEVideoRender::prepare(std::shared_ptr<VEVideoDecoder> decoder, ANativeWindow *win, int width,
                           int height, int fps) {
        try {
            std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,
                                                                       shared_from_this());
            msg->setPointer("win", win);
            msg->setInt32("width", width);
            msg->setInt32("height", height);
            msg->setInt32("fps", fps);
            msg->setObject("vdec", decoder);
            msg->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::prepare - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
    }

    VEResult VEVideoRender::prepare(VEBundle params) {

        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setPointer("win", params.get<ANativeWindow *>("surface"));
        msg->setInt32("width", params.get<int>("width"));
        msg->setInt32("height", params.get<int>("height"));
        msg->setInt32("fps", params.get<int>("fps"));
        msg->setObject("vdec", params.get<std::shared_ptr<VEVideoDecoder>>("decoder"));
        msg->post();
        return 0;
    }

    VEResult VEVideoRender::start() {
        try {
            std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::start - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
    }

    VEResult VEVideoRender::stop() {
        try {
            std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::stop - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
    }

    VEResult VEVideoRender::release() {
        try {
            std::make_shared<AMessage>(kWhatRelease, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::unInit - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
    }

    VEResult VEVideoRender::onPrepare(ANativeWindow *win) {
        ALOGI("VEVideoRender::%s", __FUNCTION__);
        JNIEnv *env = AttachCurrentThreadEnv();
        // 获取默认的 EGL 显示设备
        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL_NO_DISPLAY) {
            ALOGE("VEVideoRender::%s eglGetDisplay failed", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        // 初始化 EGL 显示设备
        EGLint major, minor;
        if (!eglInitialize(eglDisplay, &major, &minor)) {
            ALOGE("VEVideoRender::%s eglInitialize failed", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        // 配置 EGL 表面属性
        EGLint numConfigs;
        EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_NONE
        };
        if (!eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs)) {
            ALOGE("VEVideoRender::%s eglChooseConfig failed", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        // 创建 EGL 上下文
        EGLint contextAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3, // OpenGL ES 3.0
                EGL_NONE
        };
        eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext == EGL_NO_CONTEXT) {
            ALOGE("VEVideoRender::%s eglCreateContext failed", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        // 创建 EGL 表面
        eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, win, NULL);

        // 将 EGL 上下文与当前线程关联
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            ALOGE("VEVideoRender::%s eglMakeCurrent failed", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        mProgram = createProgram(vertexShaderSource, fragmentShaderSource);
        createTexture();
        ALOGD("VEVideoRender::%s mViewWidth:%d,mViewHeight:%d", __FUNCTION__, mViewWidth,
              mViewHeight);
        glViewport(0, 0, mViewWidth, mViewHeight);
        glClearColor(0.f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        return VE_OK;
    }

    VEResult VEVideoRender::onStart() {
        ALOGI("VEVideoRender::%s - Starting video render, mIsStarted=%d", __FUNCTION__, mIsStarted);
        mIsStarted = true;
        try {
            std::shared_ptr<AMessage> syncMsg = std::make_shared<AMessage>(kWhatSync,
                                                                           shared_from_this());
            syncMsg->post();
            ALOGI("VEVideoRender::%s - Posted sync message successfully", __FUNCTION__);
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::onStart - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        ALOGI("VEVideoRender::%s - Video render started successfully, mIsStarted=%d", __FUNCTION__,
              mIsStarted);
        return VE_OK;
    }

    VEResult VEVideoRender::onStop() {
        ALOGI("VEVideoRender::%s", __FUNCTION__);
        mIsStarted = false;
        return VE_OK;
    }

    VEResult VEVideoRender::onRelease() {
        ALOGI("VEVideoRender::%s", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEVideoRender::onRender(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoRender::%s enter", __FUNCTION__);
        if (!mIsStarted) {
            ALOGW("VEVideoRender::%s - render not started", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        // 检查EGL状态
        if (eglDisplay == EGL_NO_DISPLAY || eglSurface == EGL_NO_SURFACE ||
            eglContext == EGL_NO_CONTEXT) {
            ALOGE("VEVideoRender::%s - EGL not properly initialized: display=%p, surface=%p, context=%p",
                  __FUNCTION__, eglDisplay, eglSurface, eglContext);
            return UNKNOWN_ERROR;
        }

        // 确保EGL context当前绑定
        if (eglGetCurrentContext() != eglContext) {
            ALOGW("VEVideoRender::%s - EGL context not current, rebinding", __FUNCTION__);
            if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
                EGLint error = eglGetError();
                ALOGE("VEVideoRender::%s - Failed to make EGL context current, error: 0x%x",
                      __FUNCTION__, error);
                return UNKNOWN_ERROR;
            }
        }

        int32_t isDrop = false;
        msg->findInt32("drop", &isDrop);

        if (isDrop) {
            ALOGD("VEVideoRender::%s - dropping frame", __FUNCTION__);
            return VE_OK;
        }

        std::shared_ptr<VEFrame> frame = nullptr;
        std::shared_ptr<void> tmp;

        msg->findObject("render", &tmp);

        frame = std::static_pointer_cast<VEFrame>(tmp);

        if (frame == nullptr || frame->getFrame() == nullptr) {
            ALOGE("VEVideoRender::%s - frame or frame data is null", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        // 清屏
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 检查OpenGL错误
        GLenum glError = glGetError();
        if (glError != GL_NO_ERROR) {
            ALOGE("VEVideoRender::%s - OpenGL error after clear: 0x%x", __FUNCTION__, glError);
        }

        // 使用shader程序
        glUseProgram(mProgram);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            ALOGE("VEVideoRender::%s - OpenGL error after useProgram: 0x%x", __FUNCTION__, glError);
            return UNKNOWN_ERROR;
        }

        // 绑定Y纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mTextures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width,
                     frame->getFrame()->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     frame->getFrame()->data[0]);

        // 绑定U纹理
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mTextures[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width / 2,
                     frame->getFrame()->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     frame->getFrame()->data[1]);

        // 绑定V纹理
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mTextures[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width / 2,
                     frame->getFrame()->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     frame->getFrame()->data[2]);

        // 检查纹理操作错误
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            ALOGE("VEVideoRender::%s - OpenGL error after texture operations: 0x%x", __FUNCTION__,
                  glError);
        }

        mFrameWidth = frame->getFrame()->width;
        mFrameHeight = frame->getFrame()->height;
        GLint positionLoc = glGetAttribLocation(mProgram, "aPosition");
        GLint texCoordLoc = glGetAttribLocation(mProgram, "aTexCoord");
        GLint transformLoc = glGetUniformLocation(mProgram, "u_TransformMatrix");

        GLint yTextureLoc = glGetUniformLocation(mProgram, "yTexture");
        GLint uTextureLoc = glGetUniformLocation(mProgram, "uTexture");
        GLint vTextureLoc = glGetUniformLocation(mProgram, "vTexture");

        if (positionLoc < 0 || texCoordLoc < 0 || transformLoc < 0 || yTextureLoc < 0 ||
            uTextureLoc < 0 || vTextureLoc < 0) {
            ALOGE("VEVideoRender::%s - Failed to get shader locations: pos=%d, tex=%d, trans=%d, y=%d, u=%d, v=%d",
                  __FUNCTION__, positionLoc, texCoordLoc, transformLoc, yTextureLoc, uTextureLoc,
                  vTextureLoc);
            return UNKNOWN_ERROR;
        }

        glUniform1i(yTextureLoc, 0);
        glUniform1i(uTextureLoc, 1);
        glUniform1i(vTextureLoc, 2);

        ALOGD("VEVideoRender::%s ### mFrameWidth:%d,mFrameHeight:%d mViewWidth:%d,mViewHeight:%d pts:%" PRId64,
              __FUNCTION__, mFrameWidth, mFrameHeight, mViewWidth, mViewHeight,
              frame->getPts());

        float screenAspectRatio = (float) mViewWidth / mViewHeight;
        float imageAspectRatio = (float) mFrameWidth / mFrameHeight;
        float scaleX, scaleY;

        if (mFrameWidth > mFrameHeight) {
            // 横向图片
            scaleX = 1.0f;
            scaleY = screenAspectRatio / imageAspectRatio;
        } else {
            // 纵向图片
            scaleX = imageAspectRatio / screenAspectRatio;
            scaleY = 1.0f;
        }

        GLfloat vertices[] = {
                -scaleX, -scaleY, 0.0f, 0.0f,
                scaleX, -scaleY, 1.0f, 0.0f,
                -scaleX, scaleY, 0.0f, 1.0f,
                scaleX, scaleY, 1.0f, 1.0f,
        };

        glm::vec3 scaleVector(1.0f, -1.0f, 1.0f);

        // 创建一个表示旋转角度的glm::mat4
        float angle = 0.0f;
        glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angle),
                                               glm::vec3(0.0f, 0.0f, 1.0f));

        // 使用glm::scale和glm::rotate函数创建一个同时包含缩放和旋转的变换矩阵
        glm::mat4 scaleRotationMatrix = glm::scale(rotationMatrix, scaleVector);

        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(scaleRotationMatrix));

        glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
        glEnableVertexAttribArray(positionLoc);

        glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                              vertices + 2);
        glEnableVertexAttribArray(texCoordLoc);

        // 检查顶点属性设置错误
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            ALOGE("VEVideoRender::%s - OpenGL error after vertex setup: 0x%x", __FUNCTION__,
                  glError);
        }

        // 绘制
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            ALOGE("VEVideoRender::%s - OpenGL error after draw: 0x%x", __FUNCTION__, glError);
        }

        // 交换缓冲区
        if (!eglSwapBuffers(eglDisplay, eglSurface)) {
            EGLint eglError = eglGetError();
            ALOGE("VEVideoRender::%s - eglSwapBuffers failed, error: 0x%x", __FUNCTION__, eglError);
            return UNKNOWN_ERROR;
        }

        // 禁用顶点属性数组
        glDisableVertexAttribArray(positionLoc);
        glDisableVertexAttribArray(texCoordLoc);

        if (mNotify) {
            std::shared_ptr<AMessage> msg = mNotify->dup();
            msg->setInt32("type", kWhatProgress);
            msg->setInt64("progress", static_cast<int64_t>(frame->getPts()));
            msg->post();
            ALOGI("VEVideoRender::%s - Notifying progress: %" PRId64 "  what:%d", __FUNCTION__,
                  frame->getPts(), msg->what());
        }
        ALOGI("VEVideoRender::%s exit timestamp:%" PRId64, __FUNCTION__, frame->getPts());
        return VE_OK;
    }


// 加载和编译着色器
    GLuint VEVideoRender::loadShader(GLenum type, const char *shaderSrc) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &shaderSrc, NULL);
        glCompileShader(shader);
        GLint compiled;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                char *infoLog = (char *) malloc(infoLen);
                glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
                ALOGE("VEVideoRender::%s Error compiling shader:\n%s\n", __FUNCTION__, infoLog);
                free(infoLog);
            }
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint VEVideoRender::createProgram(const char *vertexSource, const char *fragmentSource) {
        GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
        if (vertexShader == 0) {
            return 0;
        }
        GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return 0;
        }
        GLuint program = glCreateProgram();
        if (program == 0) {
            return 0;
        }
        glAttachShader(program, vertexShader);
        {
            GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                ALOGE("VEVideoRender::%s Error attaching vertex shader: 0x%x", __FUNCTION__, error);
                glDeleteProgram(program);
                return 0;
            }
        }
        glAttachShader(program, fragmentShader);
        {
            GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                ALOGE("VEVideoRender::%s Error attaching fragment shader: 0x%x", __FUNCTION__,
                      error);
                glDeleteProgram(program);
                return 0;
            }
        }

        glLinkProgram(program);
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint infoLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                char *infoLog = (char *) malloc(infoLen);
                glGetProgramInfoLog(program, infoLen, NULL, infoLog);
                ALOGE("VEVideoRender::%s Error linking program:\n%s\n", __FUNCTION__, infoLog);
                free(infoLog);
            }
            glDeleteProgram(program);
            return 0;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return program;
    }

    bool VEVideoRender::createTexture() {
        glGenTextures(3, mTextures);

        for (int i = 0; i < 3; i++) {
            glBindTexture(GL_TEXTURE_2D, mTextures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        return true;
    }

    VEResult VEVideoRender::pause() {
        try {
            std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::pause - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
    }

    VEResult VEVideoRender::onPause() {
        ALOGI("VEVideoRender::%s", __FUNCTION__);
        mIsStarted = false;
        return 0;
    }

    VEResult VEVideoRender::onAVSync() {
        ALOGI("VEVideoRender::%s enter", __FUNCTION__);
        if (!mIsStarted) {
            ALOGE("VEVideoRender::%s render not started, mIsStarted=%d", __FUNCTION__, mIsStarted);
            return UNKNOWN_ERROR;
        }

        bool isDrop = false;
        std::shared_ptr<VEFrame> frame = nullptr;
        VEResult ret = mVDec->readFrame(frame);
        ALOGI("VEVideoRender::%s readFrame result: %d", __FUNCTION__, ret);

        if (ret == VE_NOT_ENOUGH_DATA) {
            ALOGI("VEVideoRender::%s needMoreFrame!!!", __FUNCTION__);
            try {
                mVDec->needMoreFrame(std::make_shared<AMessage>(kWhatSync, shared_from_this()));
            } catch (const std::bad_weak_ptr &e) {
                ALOGE("VEVideoRender::onAVSync - Object not managed by shared_ptr yet");
                return UNKNOWN_ERROR;
            }
            return VE_NOT_ENOUGH_DATA;
        }

        if (frame == nullptr) {
            ALOGE("VEVideoRender::%s onRender read frame is null!!!", __FUNCTION__);
            return UNKNOWN_ERROR;
        }
        ALOGI("VEVideoRender::onAVSync frame type: %d, pts: %" PRId64, frame->getFrameType(),
              frame->getPts());

        if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
            ALOGD("VEVideoRender::onAVSync E_FRAME_TYPE_EOF");
            std::shared_ptr<AMessage> eosMsg = mNotify->dup();
            eosMsg->setInt32("type", kWhatEOS);
            eosMsg->post();
            return UNKNOWN_ERROR;
        }

        m_AVSync->updateVideoPts(frame->getPts());

        if (m_AVSync->shouldDropFrame()) {
            ALOGI("VEVideoRender::%s Dropping frame due to sync issues", __FUNCTION__);
            isDrop = true; // 丢帧
        }

        int64_t waitTime = m_AVSync->getWaitTime(); // 获取等待时间
        ALOGD("VEVideoRender::%s waitTime:%" PRId64, __FUNCTION__, waitTime);
        try {
            std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatRender,
                                                                             shared_from_this());
            renderMsg->setObject("render", frame);
            renderMsg->setInt32("drop", isDrop);
            renderMsg->post(isDrop ? 0 : waitTime); // 根据同步状态设置等待时间
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::onAVSync - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    VEResult VEVideoRender::setSurface(ANativeWindow *win, int width, int height) {
        // 检查对象是否已经被shared_ptr管理
        try {
            std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSurfaceChanged,
                                                                       shared_from_this());
            msg->setPointer("win", win);
            msg->setInt32("width", width);
            msg->setInt32("height", height);
            msg->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoRender::setSurface - Object not managed by shared_ptr yet, storing surface info for later");
            // 如果shared_from_this失败，说明对象还没有被shared_ptr管理
            // 直接更新成员变量，等待后续处理
            mWin = win;
            mViewWidth = width;
            mViewHeight = height;
            return 0;
        }
    }

    VEResult VEVideoRender::onSurfaceChanged(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoRender::onSurfaceChanged enter");
        ANativeWindow *newWin;
        msg->findPointer("win", (void **) &newWin);
        int newWidth, newHeight;
        msg->findInt32("width", &newWidth);
        msg->findInt32("height", &newHeight);

        ALOGI("VEVideoRender::onSurfaceChanged - new surface: %p, size: %dx%d", (void*)newWin, newWidth,
              newHeight);

        if (eglDisplay == EGL_NO_DISPLAY) {
            ALOGE("VEVideoRender::onSurfaceChanged EGL display is not initialized");
            return UNKNOWN_ERROR;
        }

        // 先解绑当前的EGL Surface
        if (!eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
            ALOGE("VEVideoRender::onSurfaceChanged Failed to unbind current EGL surface");
        }

        // 释放旧的EGL Surface
        if (eglSurface != EGL_NO_SURFACE) {
            if (!eglDestroySurface(eglDisplay, eglSurface)) {
                ALOGE("VEVideoRender::onSurfaceChanged Failed to destroy old EGL surface");
            }
            eglSurface = EGL_NO_SURFACE;
        }

        // 创建新的EGL Surface
        eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, newWin, NULL);
        if (eglSurface == EGL_NO_SURFACE) {
            EGLint error = eglGetError();
            ALOGE("VEVideoRender::onSurfaceChanged eglCreateWindowSurface failed, error: 0x%x",
                  error);
            return UNKNOWN_ERROR;
        }

        // 绑定新的EGL Surface
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            EGLint error = eglGetError();
            ALOGE("VEVideoRender::onSurfaceChanged eglMakeCurrent failed, error: 0x%x", error);
            return UNKNOWN_ERROR;
        }

        // 更新视图大小
        mWin = newWin;
        mViewWidth = newWidth;
        mViewHeight = newHeight;

        // 重新设置viewport和清屏
        glViewport(0, 0, mViewWidth, mViewHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 强制刷新一帧以测试渲染
        if (!eglSwapBuffers(eglDisplay, eglSurface)) {
            EGLint error = eglGetError();
            ALOGE("VEVideoRender::onSurfaceChanged eglSwapBuffers failed, error: 0x%x", error);
        }

        ALOGI("VEVideoRender::onSurfaceChanged exit successfully, viewport: %dx%d", mViewWidth,
              mViewHeight);
        return VE_OK;
    }

    VEResult VEVideoRender::seekTo(uint64_t timestamp) {
        return 0;
    }

    VEResult VEVideoRender::flush() {
        return 0;
    }
}
