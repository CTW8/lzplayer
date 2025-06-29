#include "VEOpenGLESRender.h"
#include <iostream>

namespace VE {
    VEOpenGLESRender::VEOpenGLESRender() : eglDisplay(EGL_NO_DISPLAY), eglSurface(EGL_NO_SURFACE),
                                           eglContext(EGL_NO_CONTEXT), program(0) {}

    VEOpenGLESRender::~VEOpenGLESRender() {
        uninit();
    }

    int VEOpenGLESRender::init() {
        return initOpenGLES();
    }

    int VEOpenGLESRender::configure(const std::string &config) {
        // 配置渲染参数
        // 解析config字符串并设置相应的参数
        return 0;
    }

    void VEOpenGLESRender::start() {
        // 开始渲染
    }

    void VEOpenGLESRender::stop() {
        // 停止渲染
    }

    void VEOpenGLESRender::renderFrame(std::shared_ptr<VEFrame> frame) {
        // 使用OpenGL ES渲染帧
    }

    int VEOpenGLESRender::uninit() {
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (eglContext != EGL_NO_CONTEXT) {
                eglDestroyContext(eglDisplay, eglContext);
            }
            if (eglSurface != EGL_NO_SURFACE) {
                eglDestroySurface(eglDisplay, eglSurface);
            }
            eglTerminate(eglDisplay);
        }
        eglDisplay = EGL_NO_DISPLAY;
        eglSurface = EGL_NO_SURFACE;
        eglContext = EGL_NO_CONTEXT;
        return 0;
    }

    int VEOpenGLESRender::initOpenGLES() {
        // 初始化OpenGL ES
        // 创建EGL上下文和表面
        return 0;
    }

    GLuint VEOpenGLESRender::loadShader(GLenum type, const char *shaderSrc) {
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
                std::cerr << "Error compiling shader:\n" << infoLog << std::endl;
                free(infoLog);
            }
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint VEOpenGLESRender::createProgram(const char *vertexSource, const char *fragmentSource) {
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
                std::cerr << "Error linking program:\n" << infoLog << std::endl;
                free(infoLog);
            }
            glDeleteProgram(program);
            return 0;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return program;
    }
}