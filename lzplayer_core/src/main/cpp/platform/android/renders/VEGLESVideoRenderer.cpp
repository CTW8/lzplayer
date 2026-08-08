#include "VEGLESVideoRenderer.h"
#include "utils/VEPerfStats.h"
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

    // YUV→RGB 的系数与量程偏移由 CPU 侧按帧的 color_range/colorspace 算好传进来：
    // limited range(视频常态)必须先减 16/255 再按 255/219 展开，否则黑位停在 6% 灰、
    // 整体对比度偏低；HD 内容还要用 BT.709 而非 BT.601 的系数。
    const char *VEGLESVideoRenderer::FRAGMENT_SHADER_SOURCE = R"(
#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
uniform mat3 uColorMat;
uniform vec3 uColorOffset;
out vec4 fragColor;
void main() {
    vec3 yuv = vec3(texture(yTexture, vTexCoord).r,
                    texture(uTexture, vTexCoord).r,
                    texture(vTexture, vTexCoord).r) - uColorOffset;
    fragColor = vec4(clamp(uColorMat * yuv, 0.0, 1.0), 1.0);
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

        ALOGD("VEGLESVideoRenderer constructed");
    }

    VEGLESVideoRenderer::~VEGLESVideoRenderer() {
        ALOGD("VEGLESVideoRenderer destructed");
        uninitialize();
    }

    int VEGLESVideoRenderer::initialize(VEBundle params) {
        ALOGI("VEGLESVideoRenderer::initialize");

        // 从参数中获取必要信息
        mWin = params.get<ANativeWindow *>("surface");
        mViewWidth = params.get<int>("width");
        mViewHeight = params.get<int>("height");
        mRotationDegrees = params.get<int>("rotation");

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

        // 用容器声明的画面尺寸预分配纹理存储。不预分配的话首帧要现做
        // glTexStorage2D×3，实测软解 1080p 首帧上屏 57.2ms 而稳态只有 5.7ms。
        // 声明尺寸缺失(=0)时退回首帧分配，行为与之前一致。
        ensureTexStorage(params.get<int>("frameWidth"), params.get<int>("frameHeight"));

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

        if (win == nullptr) {
            // surface 已销毁：解绑并释放到此为止。继续去 eglCreateWindowSurface(nullptr)
            // 必然拿到 EGL_BAD_NATIVE_WINDOW，只是徒增一条错误日志。
            ALOGI("VEGLESVideoRenderer::changeSurface - surface detached");
            return VE_OK;
        }

        // 创建新的EGL Surface
        if (createEGLSurface(win) != 0) {
            ALOGE("VEGLESVideoRenderer::changeSurface - Failed to create new EGL surface");
            return VE_UNKNOWN_ERROR;
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
        return VE_OK;
    }

    VEResult VEGLESVideoRenderer::renderFrame(const std::shared_ptr<VEFrame> &frame) {
        ALOGV("VEGLESVideoRenderer::renderFrame");

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

        // 三段分开计时：上传是 CPU 侧拷贝(零拷贝可省)，swap 是等 vsync 的
        // 阻塞(零拷贝省不下来)。合成一个数字会把优化引向错误的方向
        const int64_t t0 = mPerfStats ? nowUs() : 0;
        // 更新纹理数据
        updateTextures(frame);
        const int64_t t1 = mPerfStats ? nowUs() : 0;

        // 设置uniform变量
        glUniform1i(yTextureLoc, 0);
        glUniform1i(uTextureLoc, 1);
        glUniform1i(vTextureLoc, 2);

        // YUV→RGB 的系数与量程按帧参数下发(变化时才真正写 uniform)
        updateColorConversion(frame);

        // 设置顶点属性
        setupVertexAttributes(frame);

        // 绘制
        drawFrame();
        const int64_t t2 = mPerfStats ? nowUs() : 0;

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
        if (mPerfStats) {
            mPerfStats->uploadUs.add(t1 - t0);
            mPerfStats->drawUs.add(t2 - t1);
            mPerfStats->swapUs.add(nowUs() - t2);
        }

        // 发送进度通知

//        {
//            ALOGI("linesize:%d %d %d,w:%d h:%d",frame->getFrame()->linesize[0],frame->getFrame()->linesize[1],frame->getFrame()->linesize[2],frame->getFrame()->width,frame->getFrame()->height);
//            fwrite(frame->getFrame()->data[0], frame->getFrame()->linesize[0]*frame->getFrame()->height,1,fp);
//            fwrite(frame->getFrame()->data[1], frame->getFrame()->linesize[1]*frame->getFrame()->height,1,fp);
//            fwrite(frame->getFrame()->data[2], frame->getFrame()->linesize[2]*frame->getFrame()->height,1,fp);
//        }

        ALOGV("VEGLESVideoRenderer::renderFrame - Frame rendered successfully, pts: %" PRId64, frame->getPts());
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
        colorMatLoc = glGetUniformLocation(mProgram, "uColorMat");
        colorOffsetLoc = glGetUniformLocation(mProgram, "uColorOffset");

        if (positionLoc < 0 || texCoordLoc < 0 || transformLoc < 0 ||
            yTextureLoc < 0 || uTextureLoc < 0 || vTextureLoc < 0 ||
            colorMatLoc < 0 || colorOffsetLoc < 0) {
            ALOGE("VEGLESVideoRenderer::initializeGLES - Failed to get shader locations");
            return -1;
        }
        // 强制首帧下发一次色彩参数
        mLastColorRange = -1;
        mLastColorSpace = -1;

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
        // 存储随纹理对象一起没了，下次上传要重新 glTexStorage2D
        mTexStorageReady = false;
        mTexAllocWidth = 0;
        mTexAllocHeight = 0;
    }

    // ========== 渲染相关私有方法 ==========

    void VEGLESVideoRenderer::ensureTexStorage(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        if (mTexStorageReady && mTexAllocWidth == width && mTexAllocHeight == height) {
            return;
        }
        const int chromaWidth = width / 2;
        const int chromaHeight = height / 2;
        const int dims[3][2] = {{width,       height},
                                {chromaWidth, chromaHeight},
                                {chromaWidth, chromaHeight}};
        // 尺寸变了必须重建纹理对象：glTexStorage2D 分配的是不可变存储
        if (mTexStorageReady) {
            destroyTextures();
            createTextures();
        }
        for (int i = 0; i < 3; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, mTextures[i]);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, dims[i][0], dims[i][1]);
        }
        mTexStorageReady = true;
        mTexAllocWidth = width;
        mTexAllocHeight = height;
        ALOGI("VEGLESVideoRenderer::ensureTexStorage allocated %dx%d", width, height);
    }

    void VEGLESVideoRenderer::updateTextures(const std::shared_ptr<VEFrame> &frame) {
        AVFrame *av = frame->getFrame();
        mFrameWidth = av->width;
        mFrameHeight = av->height;

        const int chromaWidth = mFrameWidth / 2;
        const int chromaHeight = mFrameHeight / 2;

        // 纹理存储只在首帧(或分辨率变化)时分配一次，之后每帧只用
        // glTexSubImage2D 覆盖像素——glTexImage2D 每帧都会重新走一遍
        // 存储分配路径，是驱动侧的无谓开销。
        ensureTexStorage(mFrameWidth, mFrameHeight);

        // 解码器输出的每行末尾通常带对齐填充(linesize > width)。用
        // GL_UNPACK_ROW_LENGTH 告诉 GL 真实行距, 就能直接上传解码器的原始缓冲,
        // 不必先在 CPU 上逐平面拷贝成紧排布(那是每帧一次全画面 memcpy)。
        auto uploadPlane = [](GLenum unit, GLuint texture, const uint8_t *data,
                              int linesize, int width, int height) {
            if (data == nullptr) {
                return;
            }
            glActiveTexture(unit);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                            GL_RED, GL_UNSIGNED_BYTE, data);
        };

        uploadPlane(GL_TEXTURE0, mTextures[0], av->data[0], av->linesize[0],
                    mFrameWidth, mFrameHeight);
        uploadPlane(GL_TEXTURE1, mTextures[1], av->data[1], av->linesize[1],
                    chromaWidth, chromaHeight);
        uploadPlane(GL_TEXTURE2, mTextures[2], av->data[2], av->linesize[2],
                    chromaWidth, chromaHeight);

        // 复位，避免影响后续其它上传
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            ALOGE("VEGLESVideoRenderer::updateTextures - OpenGL error: 0x%x", error);
        }
    }

    void VEGLESVideoRenderer::updateColorConversion(const std::shared_ptr<VEFrame> &frame) {
        AVFrame *av = frame->getFrame();

        // YUVJ* 是 full range 的历史表达；否则以 color_range 为准，
        // 未标注时按视频常态假定 limited range
        const bool fullRange =
                av->color_range == AVCOL_RANGE_JPEG ||
                av->format == AV_PIX_FMT_YUVJ420P;

        // 未标注 colorspace 时按分辨率推断：HD 及以上用 BT.709，否则 BT.601
        int space = av->colorspace;
        if (space == AVCOL_SPC_UNSPECIFIED) {
            space = (av->height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }
        const bool bt709 = (space == AVCOL_SPC_BT709);

        const int rangeKey = fullRange ? 1 : 0;
        if (mLastColorRange == rangeKey && mLastColorSpace == space) {
            return;   // 参数没变，省掉每帧两次 uniform 下发
        }
        mLastColorRange = rangeKey;
        mLastColorSpace = space;

        // yScale: limited range 的 Y 落在 [16,235]，要展开回 [0,1]
        const float yScale = fullRange ? 1.0f : (255.0f / 219.0f);
        // 色度系数：limited range 的 UV 落在 [16,240]
        const float cScale = fullRange ? 1.0f : (255.0f / 224.0f);
        // Kr/Kb 决定 YUV→RGB 的三个系数
        const float kr = bt709 ? 0.2126f : 0.299f;
        const float kb = bt709 ? 0.0722f : 0.114f;
        const float kg = 1.0f - kr - kb;

        const float vToR = cScale * 2.0f * (1.0f - kr);
        const float uToB = cScale * 2.0f * (1.0f - kb);
        const float vToG = -cScale * 2.0f * (1.0f - kr) * kr / kg;
        const float uToG = -cScale * 2.0f * (1.0f - kb) * kb / kg;

        // GLSL mat3 按列优先：col0 乘 Y，col1 乘 U，col2 乘 V
        const GLfloat mat[9] = {
                yScale, yScale, yScale,   // col0: Y → R,G,B
                0.0f,   uToG,   uToB,     // col1: U → R,G,B
                vToR,   vToG,   0.0f,     // col2: V → R,G,B
        };
        const GLfloat offset[3] = {
                fullRange ? 0.0f : (16.0f / 255.0f),
                128.0f / 255.0f,
                128.0f / 255.0f,
        };

        glUniformMatrix3fv(colorMatLoc, 1, GL_FALSE, mat);
        glUniform3fv(colorOffsetLoc, 1, offset);
        ALOGI("VEGLESVideoRenderer color conversion: %s range, %s",
              fullRange ? "full" : "limited", bt709 ? "BT.709" : "BT.601");
    }

    void VEGLESVideoRenderer::setupVertexAttributes(const std::shared_ptr<VEFrame> &frame) {
        // 计算变换矩阵
        glm::mat4 transformMatrix;
        calculateTransformMatrix(mFrameWidth, mFrameHeight, transformMatrix);
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(transformMatrix));

        // 计算缩放比例：fit-inside 按宽高比大小比较，不能按帧的横竖朝向判断，
        // 否则横屏视频在竖屏上会放大裁掉大半画面
        float scaleX = 1.0f, scaleY = 1.0f;
        if (mViewWidth > 0 && mViewHeight > 0 && mFrameWidth > 0 && mFrameHeight > 0) {
            // 旋转 90/270 后画幅的长短边互换，宽高比要按旋转后的算
            const bool swapped = (mRotationDegrees == 90 || mRotationDegrees == 270);
            const int dispW = swapped ? mFrameHeight : mFrameWidth;
            const int dispH = swapped ? mFrameWidth : mFrameHeight;
            float screenAspectRatio = (float) mViewWidth / mViewHeight;
            float imageAspectRatio = (float) dispW / dispH;
            if (imageAspectRatio > screenAspectRatio) {
                // 画面比屏幕宽：横向占满，纵向留黑边
                scaleX = 1.0f;
                scaleY = screenAspectRatio / imageAspectRatio;
            } else {
                // 画面比屏幕窄：纵向占满，横向留黑边
                scaleX = imageAspectRatio / screenAspectRatio;
                scaleY = 1.0f;
            }
        }

        // 旋转靠重排纹理坐标实现。四个顶点按 TRIANGLE_STRIP 的顺序是
        // 左上/右上/左下/右下(已计入 Y 翻转)，只要把它们各自采样的纹理角点
        // 换一圈，画面就转过来了，且不受视口宽高比影响。
        static const GLfloat kTexCoords[4][8] = {
                /*   0° */ {0, 0,  1, 0,  0, 1,  1, 1},
                /*  90° */ {0, 1,  0, 0,  1, 1,  1, 0},
                /* 180° */ {1, 1,  0, 1,  1, 0,  0, 0},
                /* 270° */ {1, 0,  1, 1,  0, 0,  0, 1},
        };
        int rotIndex = ((mRotationDegrees % 360) + 360) % 360 / 90;
        if (rotIndex < 0 || rotIndex > 3) {
            rotIndex = 0;
        }
        const GLfloat *tex = kTexCoords[rotIndex];

        // 顶点数据必须常驻成员：glVertexAttribPointer 记录的是客户端指针，
        // glDrawArrays 在本函数返回之后才执行。之前用函数级 static 数组，
        // 初始化只跑一次，换视频/转屏后画幅比例永远停在进程第一帧。
        const GLfloat vertices[] = {
                -scaleX, -scaleY, tex[0], tex[1],
                 scaleX, -scaleY, tex[2], tex[3],
                -scaleX,  scaleY, tex[4], tex[5],
                 scaleX,  scaleY, tex[6], tex[7],
        };
        memcpy(mVertices, vertices, sizeof(vertices));

        // 设置顶点属性
        glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), mVertices);
        glEnableVertexAttribArray(positionLoc);

        glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), mVertices + 2);
        glEnableVertexAttribArray(texCoordLoc);
    }

    void VEGLESVideoRenderer::calculateTransformMatrix(int frameWidth, int frameHeight, glm::mat4& transformMatrix) {
        // 只做 Y 轴翻转：纹理坐标原点在左上，GL 裁剪空间原点在左下。
        //
        // 旋转**不在这里做**。裁剪空间不是等比的(视口通常不是正方形)，
        // 在里面转 90° 会把画面按视口宽高比拉变形。旋转改由纹理坐标承担
        // (见 setupVertexAttributes)，那是无量纲空间，转多少度都不会形变。
        transformMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
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