#include "VEVideoRender.h"



void VEVideoRender::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatInit:{
            msg->findPointer("win",(void**)&mWin);
            std::shared_ptr<void> tmp;
            msg->findObject("vdec",&tmp);
            mVDec = std::static_pointer_cast<VEVideoDecoder>(tmp);

            onInit(mWin);
            break;
        }
        case kWhatStart:{
            onStart();
            break;
        }
        case kWhatRender:{
            if(onRender()){
                std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRender,shared_from_this());
                msg->post();
            }
            break;
        }
        case kWhatStop:{
            onStop();
            break;
        }
        case kWhatUninit:{
            onUnInit();
            break;
        }
    }
}

VEVideoRender::VEVideoRender() {

}

VEVideoRender::~VEVideoRender() {

}

status_t VEVideoRender::init(std::shared_ptr<VEVideoDecoder> decoder,ANativeWindow *win) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,shared_from_this());
    msg->setPointer("win",win);
    msg->setObject("vdec",decoder);
    msg->post();
    return 0;
}

status_t VEVideoRender::start() {
    std::make_shared<AMessage>(kWhatStart,shared_from_this())->post();
    return 0;
}

status_t VEVideoRender::stop() {
    std::make_shared<AMessage>(kWhatStop,shared_from_this())->post();
    return 0;
}

status_t VEVideoRender::unInit() {
    std::make_shared<AMessage>(kWhatUninit,shared_from_this())->post();
    return 0;
}

bool VEVideoRender::onInit(ANativeWindow * win) {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        // 处理错误
        ALOGE("eglGetDisplay failed!!");
        return false;
    }
    if (!eglInitialize(eglDisplay, 0, 0)) {
        // 处理错误
        ALOGE("eglInitialize failed!!");
        return false;
    }
    // 配置 EGL
    EGLConfig eglConfig;
    EGLint numConfigs;
    const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
    };
    if (!eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs) || (numConfigs < 1)) {
        // 处理错误
        ALOGE("eglChooseConfig failed!!");
        return false;
    }

    // 创建 EGL 表面
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig,
                                                (EGLNativeWindowType)win, nullptr);
    if (eglSurface == EGL_NO_SURFACE) {
        // 处理错误
        ALOGE("eglCreateWindowSurface failed!!");
        return false;
    }

    // 创建 OpenGL ES 上下文
    const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    EGLContext eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext == EGL_NO_CONTEXT) {
        // 处理错误
        ALOGE("eglCreateContext failed!!");
        return false;
    }

    // 绑定上下文
    if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
        // 处理错误
        ALOGE("eglMakeCurrent failed!!");
        return false;
    }
    const char *version = (const char*)glGetString(GL_VERSION);
    ALOGI("opengles version:%s",version);
    createPragram();
    createTexture();
    return true;
}

bool VEVideoRender::onStart() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    mIsStarted = true;
    std::make_shared<AMessage>(kWhatRender,shared_from_this())->post();
    return false;
}

bool VEVideoRender::onStop() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    mIsStarted = false;
    return false;
}

bool VEVideoRender::onUnInit() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    return false;
}

bool VEVideoRender::onRender() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    if(!mIsStarted){
        return false;
    }

    std::shared_ptr<VEFrame> frame = nullptr;

    mVDec->readFrame(frame);

    glUseProgram(mProgram);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindVertexArray(mVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width, frame->getFrame()->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mTexture[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width / 2, frame->getFrame()->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[1]);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mTexture[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width / 2, frame->getFrame()->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[2]);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(eglDisplay,eglSurface);

    return true;
}
const char* vertexShaderSource =R"(
#version 300 es
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
})";

const char * fragmentShaderSource =R"(
#version 300 es
precision mediump float;
in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
void main() {
    float y = texture(yTexture, vTexCoord).r;
    float u = texture(uTexture, vTexCoord).r - 0.5;
    float v = texture(vTexture, vTexCoord).r - 0.5;

    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;

    outColor = vec4(r, g, b, 1.0);
})";

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
            char *infoLog = (char *)malloc(infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            ALOGE("Error compiling shader:\n%s\n", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool VEVideoRender::createPragram() {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (!vertexShader) return 0;

    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (!fragmentShader) return 0;

    // 链接程序
    GLuint mProgram = glCreateProgram();
    glAttachShader(mProgram, vertexShader);
    glAttachShader(mProgram, fragmentShader);
    glLinkProgram(mProgram);

    GLint success;
    // 检查链接错误
    glGetProgramiv(mProgram, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(mProgram, 512, NULL, infoLog);
        ALOGE("%s",infoLog);
        return false;
    }

    // 删除着色器，它们已经链接到程序中，不再需要了
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

bool VEVideoRender::createTexture() {
    // 创建纹理，初始化 OpenGL 状态
    glGenTextures(3,mTexture);
    for(int i =0;i<3;i++){
        glBindTexture(GL_TEXTURE_2D, mTexture[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }


    // 创建和绑定 VAO 和 VBO
    glGenVertexArrays(1, &mVAO);
    glBindVertexArray(mVAO);

    glGenBuffers(1, &mVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    static const GLfloat vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            -1.0f,  1.0f,
            1.0f,  1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);

    GLuint texCoordBuffer;
    glGenBuffers(1, &texCoordBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, texCoordBuffer);
    static const GLfloat texCoordData[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(texCoordData), texCoordData, GL_STATIC_DRAW);

    GLint posLoc = glGetAttribLocation(mProgram, "aPosition");
    GLint texLoc = glGetAttribLocation(mProgram, "aTexCoord");

    GLint yTextureLoc = glGetUniformLocation(mProgram, "yTexture");
    GLint uTextureLoc = glGetUniformLocation(mProgram, "uTexture");
    GLint vTextureLoc = glGetUniformLocation(mProgram, "vTexture");

    glUniform1i(yTextureLoc, 0);
    glUniform1i(uTextureLoc, 1);
    glUniform1i(vTextureLoc, 2);

    glEnableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);

    glEnableVertexAttribArray(texLoc);
    glBindBuffer(GL_ARRAY_BUFFER, texCoordBuffer);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);

    glBindVertexArray(0);
    return true;
}

status_t VEVideoRender::pause() {
    return 0;
}

status_t VEVideoRender::resume() {
    return 0;
}

bool VEVideoRender::onPause() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    return false;
}

bool VEVideoRender::onReume() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    return false;
}
