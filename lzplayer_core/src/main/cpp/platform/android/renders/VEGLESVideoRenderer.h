#ifndef __VE_OPENGLES_RENDER__
#define __VE_OPENGLES_RENDER__

#include "IVideoRender.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <android/native_window.h>
#include "glm/fwd.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/ext/matrix_projection.hpp"

namespace VE {

    class VEGLESVideoRenderer : public IVideoRender {
    public:
        VEGLESVideoRenderer();
        ~VEGLESVideoRenderer() override;

        // IVideoRender接口实现
        VEResult initialize(VEBundle params) override;
        VEResult changeSurface(ANativeWindow *win,int viewWidth,int viewHeight) override;

        void setPerfStats(const std::shared_ptr<VEPerfStats> &stats) override {
            mPerfStats = stats;
        }
        VEResult renderFrame(const std::shared_ptr<VEFrame> &frame) override;
        VEResult uninitialize() override;

    private:
        // EGL相关方法
        int initializeEGL(ANativeWindow *win);
        void destroyEGL();
        int createEGLSurface(ANativeWindow *win);
        void destroyEGLSurface();

        // OpenGL ES相关方法
        int initializeGLES();
        void destroyGLES();
        GLuint loadShader(GLenum type, const char *shaderSrc);
        GLuint createProgram(const char *vertexSource, const char *fragmentSource);
        bool createTextures();

        /// 按给定尺寸确保纹理存储就绪。尺寸变化会重建(glTexStorage2D 分配的
        /// 是不可变存储)。prepare 阶段用容器声明的尺寸先分配，首帧就不必现分配
        void ensureTexStorage(int width, int height);
        void destroyTextures();

        // 渲染相关方法
        void updateTextures(const std::shared_ptr<VEFrame> &frame);
        void setupVertexAttributes(const std::shared_ptr<VEFrame> &frame);
        void calculateTransformMatrix(int frameWidth, int frameHeight, glm::mat4& transformMatrix);
        void drawFrame();

        /// 按帧的 color_range/colorspace 选 YUV→RGB 系数与量程偏移，
        /// 结果写进 uColorMat/uColorOffset。同参数重复调用会跳过。
        void updateColorConversion(const std::shared_ptr<VEFrame> &frame);

        // EGL上下文相关
        EGLDisplay eglDisplay;
        EGLSurface eglSurface;
        EGLContext eglContext;
        EGLConfig eglConfig;

        // OpenGL ES资源
        GLuint mProgram;
        GLuint mTextures[3];  // Y, U, V纹理
        /// 顶点数据(位置+纹理坐标交错)。glVertexAttribPointer 记录的是
        /// 客户端指针，draw 时才读取，必须常驻成员
        GLfloat mVertices[16] = {0};

        // Shader位置
        GLint positionLoc;
        GLint texCoordLoc;
        GLint transformLoc;
        GLint yTextureLoc;
        GLint uTextureLoc;
        GLint vTextureLoc;
        GLint colorMatLoc;
        GLint colorOffsetLoc;

        // 视图参数
        ANativeWindow *mWin;
        int mViewWidth;
        int mViewHeight;
        int mFrameWidth;
        int mFrameHeight;
        /// 容器标注的旋转角(0/90/180/270)：竖拍视频必须靠它摆正，
        /// 否则画面横躺。同时影响 fit-inside 的宽高比计算。
        int mRotationDegrees = 0;

        /// 上一次生效的色彩参数，用于跳过重复的 uniform 设置
        int mLastColorRange = -1;
        int mLastColorSpace = -1;
        /// 纹理存储是否已按当前尺寸分配(glTexStorage2D 只做一次)
        /// 稳态指标，由显示端注入。只在渲染线程写
        std::shared_ptr<VEPerfStats> mPerfStats;
        bool mTexStorageReady = false;
        int mTexAllocWidth = 0;
        int mTexAllocHeight = 0;

        // 初始化状态
        bool mEGLInitialized;
        bool mGLESInitialized;

        // Shader源码
        static const char *VERTEX_SHADER_SOURCE;
        static const char *FRAGMENT_SHADER_SOURCE;
    };

} // namespace VE
#endif 