#ifndef __VE_OPENGLES_RENDER__
#define __VE_OPENGLES_RENDER__

#include "IVideoRender.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <memory>
namespace VE {
    class VEOpenGLESRender : public IVideoRender {
    public:
        VEOpenGLESRender();

        ~VEOpenGLESRender();

        // 初始化渲染器
        int init();

        // 配置渲染器参数
        int configure(const std::string &config);

        // 开始渲染
        void start() override;

        // 停止渲染
        void stop() override;

        // 渲染帧
        void renderFrame(std::shared_ptr<VEFrame> frame);

        // 释放渲染器资源
        int uninit();

    private:
        EGLDisplay eglDisplay;
        EGLSurface eglSurface;
        EGLContext eglContext;
        GLuint program;
        GLuint textures[3];

        // 初始化OpenGL ES
        int initOpenGLES();

        // 加载着色器
        GLuint loadShader(GLenum type, const char *shaderSrc);

        // 创建OpenGL程序
        GLuint createProgram(const char *vertexSource, const char *fragmentSource);
    };
}
#endif 