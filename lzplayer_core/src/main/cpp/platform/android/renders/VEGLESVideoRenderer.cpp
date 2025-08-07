#include "VEGLESVideoRenderer.h"
#include "VEJvmOnLoad.h"

namespace VE {

    const char *VEGLESVideoRenderer::VERTEX_SHADER_SOURCE = R"(
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

    const char *VEGLESVideoRenderer::FRAGMENT_SHADER_SOURCE = R"(
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

    VEGLESVideoRenderer::VEGLESVideoRenderer()
            : eglDisplay(EGL_NO_DISPLAY),
              eglSurface(EGL_NO_SURFACE),
              eglContext(EGL_NO_CONTEXT),
              mProgram(0),
              mWin(nullptr),
              mViewWidth(0),
              mViewHeight(0),
              mFrameWidth(0),
              mFrameHeight(0),
              mEGLInitialized(false),
              mGLESInitialized(false) {

        // 初始化纹理ID为0
        memset(mTextures, 0, sizeof(mTextures));

        // 初始化shader位置为-1
        positionLoc = -1;
        texCoordLoc = -1;
        transformLoc = -1;
        yTextureLoc = -1;
        uTextureLoc = -1;
        vTextureLoc = -1;

        fp = fopen("/data/local/tmp/dump_video.rgb","wb+");

        ALOGD("VEGLESVideoRenderer constructed");
    }

    VEGLESVideoRenderer::~VEGLESVideoRenderer() {
        ALOGD("VEGLESVideoRenderer destructed");
        uninitialize();
        if(fp){
            fflush(fp);
            fclose(fp);
        }
    }

    int VEGLESVideoRenderer::initialize(VEBundle params) {
        ALOGI("VEGLESVideoRenderer::initialize");

        // 从参数中获取必要信息
        mWin = params.get<ANativeWindow *>("surface");
        mViewWidth = params.get<int>("width");
        mViewHeight = params.get<int>("height");

        if (mWin == nullptr) {
            ALOGE("VEGLESVideoRenderer::initialize - Invalid surface");
            return -1;
        }

        // 初始化EGL
        if (initializeEGL(mWin) != 0) {
            ALOGE("VEGLESVideoRenderer::initialize - Failed to initialize EGL");
            return -1;
        }

        // 初始化OpenGL ES
        if (initializeGLES() != 0) {
            ALOGE("VEGLESVideoRenderer::initialize - Failed to initialize OpenGL ES");
            destroyEGL();
            return -1;
        }

        // 设置视口和清屏
        glViewport(0, 0, mViewWidth, mViewHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ALOGI("VEGLESVideoRenderer::initialize - Success, viewport: %dx%d", mViewWidth, mViewHeight);
        return 0;
    }

    VEResult VEGLESVideoRenderer::changeSurface(ANativeWindow *win,int viewWidth,int viewHeight) {
        ALOGI("VEGLESVideoRenderer::changeSurface - new surface: %p", (void*)win);

        if (!mEGLInitialized) {
            ALOGE("VEGLESVideoRenderer::changeSurface - EGL not initialized");
            return 0;
        }

        // 先解绑当前的EGL Surface
        if (!eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
            ALOGE("VEGLESVideoRenderer::changeSurface - Failed to unbind current EGL surface");
        }

        // 销毁旧的EGL Surface
        destroyEGLSurface();

        // 更新窗口引用
        mWin = win;
        mViewWidth = viewWidth;
        mViewHeight = viewHeight;

        // 创建新的EGL Surface
        if (createEGLSurface(win) != 0) {
            ALOGE("VEGLESVideoRenderer::changeSurface - Failed to create new EGL surface");
            return 0;
        }

        // 重新设置viewport
        glViewport(0, 0, mViewWidth, mViewHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 强制刷新一帧
        if (!eglSwapBuffers(eglDisplay, eglSurface)) {
            EGLint error = eglGetError();
            ALOGE("VEGLESVideoRenderer::changeSurface - eglSwapBuffers failed, error: 0x%x", error);
        }

        ALOGI("VEGLESVideoRenderer::changeSurface - Surface changed successfully");
    }

    VEResult VEGLESVideoRenderer::renderFrame(const std::shared_ptr<VEFrame> &frame) {
        ALOGI("VEGLESVideoRenderer::renderFrame");

        if (!mEGLInitialized || !mGLESInitialized) {
            ALOGE("VEGLESVideoRenderer::renderFrame - Renderer not properly initialized");
            return 0;
        }

        if (frame == nullptr || frame->getFrame() == nullptr) {
            ALOGE("VEGLESVideoRenderer::renderFrame - Invalid frame data");
            return 0;
        }

        // 确保EGL context当前绑定
        if (eglGetCurrentContext() != eglContext) {
            ALOGW("VEGLESVideoRenderer::renderFrame - EGL context not current, rebinding");
            if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
                EGLint error = eglGetError();
                ALOGE("VEGLESVideoRenderer::renderFrame - Failed to make EGL context current, error: 0x%x", error);
                return 0;
            }
        }

        // 清屏
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 使用shader程序
        glUseProgram(mProgram);
        GLenum glError = glGetError();
        if (glError != GL_NO_ERROR) {
            ALOGE("VEGLESVideoRenderer::renderFrame - OpenGL error after useProgram: 0x%x", glError);
            return 0;
        }

        // 更新纹理数据
        updateTextures(frame);

        // 设置uniform变量
        glUniform1i(yTextureLoc, 0);
        glUniform1i(uTextureLoc, 1);
        glUniform1i(vTextureLoc, 2);

        // 设置顶点属性
        setupVertexAttributes(frame);

        // 绘制
        drawFrame();

//        {
//            int buf_size = mViewWidth*mViewHeight*4;
//            uint8_t *buf = (uint8_t *) malloc(buf_size);
//            glReadPixels(0, 0, mViewWidth, mViewHeight,
//                         GL_RGBA, GL_UNSIGNED_BYTE,
//                         buf);
//            fwrite(buf,buf_size,1,fp);
//            free(buf);
//        }

        // 交换缓冲区
        if (!eglSwapBuffers(eglDisplay, eglSurface)) {
            EGLint eglError = eglGetError();
            ALOGE("VEGLESVideoRenderer::renderFrame - eglSwapBuffers failed, error: 0x%x", eglError);
            return 0;
        }

        // 发送进度通知

//        {
//            ALOGI("linesize:%d %d %d,w:%d h:%d",frame->getFrame()->linesize[0],frame->getFrame()->linesize[1],frame->getFrame()->linesize[2],frame->getFrame()->width,frame->getFrame()->height);
//            fwrite(frame->getFrame()->data[0], frame->getFrame()->linesize[0]*frame->getFrame()->height,1,fp);
//            fwrite(frame->getFrame()->data[1], frame->getFrame()->linesize[1]*frame->getFrame()->height,1,fp);
//            fwrite(frame->getFrame()->data[2], frame->getFrame()->linesize[2]*frame->getFrame()->height,1,fp);
//        }

        ALOGI("VEGLESVideoRenderer::renderFrame - Frame rendered successfully, pts: %" PRId64, frame->getPts());
        return VE_OK;
    }

    int VEGLESVideoRenderer::uninitialize() {
        ALOGI("VEGLESVideoRenderer::uninitialize");

        destroyGLES();
        destroyEGL();

        mWin = nullptr;
        mViewWidth = 0;
        mViewHeight = 0;
        mFrameWidth = 0;
        mFrameHeight = 0;

        ALOGI("VEGLESVideoRenderer::uninitialize - Resources released");
        return 0;
    }

    // ========== EGL相关私有方法 ==========

    int VEGLESVideoRenderer::initializeEGL(ANativeWindow *win) {
        ALOGI("VEGLESVideoRenderer::initializeEGL");

        // 获取默认的 EGL 显示设备
        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL_NO_DISPLAY) {
            ALOGE("VEGLESVideoRenderer::initializeEGL - eglGetDisplay failed");
            return -1;
        }

        // 初始化 EGL 显示设备
        EGLint major, minor;
        if (!eglInitialize(eglDisplay, &major, &minor)) {
            ALOGE("VEGLESVideoRenderer::initializeEGL - eglInitialize failed");
            return -1;
        }

        // 配置 EGL 表面属性
        EGLint numConfigs;
        EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_NONE
        };
        if (!eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs)) {
            ALOGE("VEGLESVideoRenderer::initializeEGL - eglChooseConfig failed");
            eglTerminate(eglDisplay);
            return -1;
        }

        // 创建 EGL 上下文
        EGLint contextAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3, // OpenGL ES 3.0
                EGL_NONE
        };
        eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext == EGL_NO_CONTEXT) {
            ALOGE("VEGLESVideoRenderer::initializeEGL - eglCreateContext failed");
            eglTerminate(eglDisplay);
            return -1;
        }

        // 创建EGL Surface
        if (createEGLSurface(win) != 0) {
            ALOGE("VEGLESVideoRenderer::initializeEGL - Failed to create EGL surface");
            eglDestroyContext(eglDisplay, eglContext);
            eglTerminate(eglDisplay);
            return -1;
        }

        mEGLInitialized = true;
        ALOGI("VEGLESVideoRenderer::initializeEGL - EGL initialized successfully");
        return 0;
    }

    void VEGLESVideoRenderer::destroyEGL() {
        ALOGI("VEGLESVideoRenderer::destroyEGL");

        if (!mEGLInitialized) {
            return;
        }

        // 解绑EGL context
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }

        // 销毁EGL Surface
        destroyEGLSurface();

        // 销毁EGL Context
        if (eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay, eglContext);
            eglContext = EGL_NO_CONTEXT;
        }

        // 终止EGL Display
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
        }

        mEGLInitialized = false;
        ALOGI("VEGLESVideoRenderer::destroyEGL - EGL resources destroyed");
    }

    int VEGLESVideoRenderer::createEGLSurface(ANativeWindow *win) {
        // 创建 EGL 表面
        eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, win, NULL);
        if (eglSurface == EGL_NO_SURFACE) {
            EGLint error = eglGetError();
            ALOGE("VEGLESVideoRenderer::createEGLSurface - eglCreateWindowSurface failed, error: 0x%x", error);
            return -1;
        }

        // 将 EGL 上下文与当前线程关联
        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            EGLint error = eglGetError();
            ALOGE("VEGLESVideoRenderer::createEGLSurface - eglMakeCurrent failed, error: 0x%x", error);
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
            return -1;
        }

        return 0;
    }

    void VEGLESVideoRenderer::destroyEGLSurface() {
        if (eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
        }
    }

    // ========== OpenGL ES相关私有方法 ==========

    int VEGLESVideoRenderer::initializeGLES() {
        ALOGI("VEGLESVideoRenderer::initializeGLES");

        // 创建shader程序
        mProgram = createProgram(VERTEX_SHADER_SOURCE, FRAGMENT_SHADER_SOURCE);
        if (mProgram == 0) {
            ALOGE("VEGLESVideoRenderer::initializeGLES - Failed to create shader program");
            return -1;
        }

        // 获取shader变量位置
        positionLoc = glGetAttribLocation(mProgram, "aPosition");
        texCoordLoc = glGetAttribLocation(mProgram, "aTexCoord");
        transformLoc = glGetUniformLocation(mProgram, "u_TransformMatrix");
        yTextureLoc = glGetUniformLocation(mProgram, "yTexture");
        uTextureLoc = glGetUniformLocation(mProgram, "uTexture");
        vTextureLoc = glGetUniformLocation(mProgram, "vTexture");

        if (positionLoc < 0 || texCoordLoc < 0 || transformLoc < 0 ||
            yTextureLoc < 0 || uTextureLoc < 0 || vTextureLoc < 0) {
            ALOGE("VEGLESVideoRenderer::initializeGLES - Failed to get shader locations");
            return -1;
        }

        // 创建纹理
        if (!createTextures()) {
            ALOGE("VEGLESVideoRenderer::initializeGLES - Failed to create textures");
            return -1;
        }

        mGLESInitialized = true;
        ALOGI("VEGLESVideoRenderer::initializeGLES - OpenGL ES initialized successfully");
        return 0;
    }

    void VEGLESVideoRenderer::destroyGLES() {
        ALOGI("VEGLESVideoRenderer::destroyGLES");

        if (!mGLESInitialized) {
            return;
        }

        destroyTextures();

        if (mProgram != 0) {
            glDeleteProgram(mProgram);
            mProgram = 0;
        }

        mGLESInitialized = false;
        ALOGI("VEGLESVideoRenderer::destroyGLES - OpenGL ES resources destroyed");
    }

    GLuint VEGLESVideoRenderer::loadShader(GLenum type, const char *shaderSrc) {
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
                ALOGE("VEGLESVideoRenderer::loadShader - Error compiling shader:\n%s\n", infoLog);
                free(infoLog);
            }
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint VEGLESVideoRenderer::createProgram(const char *vertexSource, const char *fragmentSource) {
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
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return 0;
        }

        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint infoLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                char *infoLog = (char *) malloc(infoLen);
                glGetProgramInfoLog(program, infoLen, NULL, infoLog);
                ALOGE("VEGLESVideoRenderer::createProgram - Error linking program:\n%s\n", infoLog);
                free(infoLog);
            }
            glDeleteProgram(program);
            program = 0;
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return program;
    }

    bool VEGLESVideoRenderer::createTextures() {
        glGenTextures(3, mTextures);

        for (int i = 0; i < 3; i++) {
            glBindTexture(GL_TEXTURE_2D, mTextures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            ALOGE("VEGLESVideoRenderer::createTextures - OpenGL error: 0x%x", error);
            return false;
        }

        return true;
    }

    void VEGLESVideoRenderer::destroyTextures() {
        if (mTextures[0] != 0) {
            glDeleteTextures(3, mTextures);
            memset(mTextures, 0, sizeof(mTextures));
        }
    }

    // ========== 渲染相关私有方法 ==========

    void VEGLESVideoRenderer::updateTextures(const std::shared_ptr<VEFrame> &frame) {
        mFrameWidth = frame->getFrame()->width;
        mFrameHeight = frame->getFrame()->height;

        // 绑定Y纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mTextures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mFrameWidth, mFrameHeight,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[0]);

        // 绑定U纹理
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mTextures[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mFrameWidth / 2, mFrameHeight / 2,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[1]);

        // 绑定V纹理
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mTextures[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mFrameWidth / 2, mFrameHeight / 2,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[2]);

        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            ALOGE("VEGLESVideoRenderer::updateTextures - OpenGL error: 0x%x", error);
        }
    }

    void VEGLESVideoRenderer::setupVertexAttributes(const std::shared_ptr<VEFrame> &frame) {
        // 计算变换矩阵
        glm::mat4 transformMatrix;
        calculateTransformMatrix(mFrameWidth, mFrameHeight, transformMatrix);
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(transformMatrix));

        // 计算缩放比例
        float screenAspectRatio = (float) mViewWidth / mViewHeight;
        float imageAspectRatio = (float) mFrameWidth / mFrameHeight;
        float scaleX, scaleY;

        if (mFrameWidth < mFrameHeight) {
            // 横向图片
            scaleX = 1.0f;
            scaleY = screenAspectRatio / imageAspectRatio;
        } else {
            // 纵向图片
            scaleX = imageAspectRatio / screenAspectRatio;
            scaleY = 1.0f;
        }

        static GLfloat vertices[] = {
                -scaleX, -scaleY, 0.0f, 0.0f,
                scaleX, -scaleY, 1.0f, 0.0f,
                -scaleX, scaleY, 0.0f, 1.0f,
                scaleX, scaleY, 1.0f, 1.0f,
        };

        // 设置顶点属性
        glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
        glEnableVertexAttribArray(positionLoc);

        glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
        glEnableVertexAttribArray(texCoordLoc);
    }

    void VEGLESVideoRenderer::calculateTransformMatrix(int frameWidth, int frameHeight, glm::mat4& transformMatrix) {
        glm::vec3 scaleVector(1.0f, -1.0f, 1.0f);
        float angle = 0.0f;
        glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));
        transformMatrix = glm::scale(rotationMatrix, scaleVector);
    }

    void VEGLESVideoRenderer::drawFrame() {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            ALOGE("VEGLESVideoRenderer::drawFrame - OpenGL error: 0x%x", error);
        }

        // 禁用顶点属性数组
        glDisableVertexAttribArray(positionLoc);
        glDisableVertexAttribArray(texCoordLoc);
    }

} // namespace VE