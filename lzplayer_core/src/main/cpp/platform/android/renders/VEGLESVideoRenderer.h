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
        void destroyTextures();

        // 渲染相关方法
        void updateTextures(const std::shared_ptr<VEFrame> &frame);
        void setupVertexAttributes(const std::shared_ptr<VEFrame> &frame);
        void calculateTransformMatrix(int frameWidth, int frameHeight, glm::mat4& transformMatrix);
        void drawFrame();

        // EGL上下文相关
        EGLDisplay eglDisplay;
        EGLSurface eglSurface;
        EGLContext eglContext;
        EGLConfig eglConfig;

        // OpenGL ES资源
        GLuint mProgram;
        GLuint mTextures[3];  // Y, U, V纹理

        // Shader位置
        GLint positionLoc;
        GLint texCoordLoc;
        GLint transformLoc;
        GLint yTextureLoc;
        GLint uTextureLoc;
        GLint vTextureLoc;

        // 视图参数
        ANativeWindow *mWin;
        int mViewWidth;
        int mViewHeight;
        int mFrameWidth;
        int mFrameHeight;

        // 初始化状态
        bool mEGLInitialized;
        bool mGLESInitialized;

        FILE *fp= nullptr;
        // Shader源码
        static const char *VERTEX_SHADER_SOURCE;
        static const char *FRAGMENT_SHADER_SOURCE;
    };

} // namespace VE
#endif 