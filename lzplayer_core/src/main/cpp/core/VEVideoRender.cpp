#include "VEVideoRender.h"
#include "VEJvmOnLoad.h"
#include "glm/fwd.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/ext/matrix_projection.hpp"
#include "VEPlayer.h"
#include "VEAVsync.h"

const char* vertexShaderSource = R"(
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

const char* fragmentShaderSource = R"(
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
        case kWhatInit:{
            msg->findPointer("win",(void**)&mWin);
            std::shared_ptr<void> tmp = nullptr;

            msg->findObject("vdec",&tmp);
            mVDec = std::static_pointer_cast<VEVideoDecoder>(tmp);

            msg->findInt32("width",&mViewWidth);
            msg->findInt32("height",&mViewHeight);
            onInit(mWin);
            break;
        }
        case kWhatStart:{
            onStart();
            break;
        }
        case kWhatSync:{
            if(onAVSync() != VE_OK){
                ///todo
            }
            break;
        }
        case kWhatRender:{
            if(onRender(msg) == VE_OK){
                std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatSync,shared_from_this());
                renderMsg->post();
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
        case kWhatPause:{
            mIsStarted = false;
            break;
        }
        case kWhatResume:{
            mIsStarted = true;
            std::make_shared<AMessage>(kWhatSync,shared_from_this())->post();
            break;
        }
        default:{
            break;
        }
    }
}

VEVideoRender::VEVideoRender(std::shared_ptr<AMessage> notify, std::shared_ptr<VEAVsync> avSync)
    : mNotify(notify), m_AVSync(avSync) {}

VEVideoRender::~VEVideoRender() {
    stop();
}

VEResult VEVideoRender::init(std::shared_ptr<VEVideoDecoder> decoder, ANativeWindow *win, int width, int height, int fps) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,shared_from_this());
    msg->setPointer("win",win);
    msg->setInt32("width",width);
    msg->setInt32("height",height);
    msg->setInt32("fps",fps);
    msg->setObject("vdec",decoder);
    msg->post();
    return 0;
}

VEResult VEVideoRender::start() {
    std::make_shared<AMessage>(kWhatStart,shared_from_this())->post();
    return 0;
}

VEResult VEVideoRender::stop() {
    std::make_shared<AMessage>(kWhatStop,shared_from_this())->post();
    return 0;
}

VEResult VEVideoRender::unInit() {
    std::make_shared<AMessage>(kWhatUninit,shared_from_this())->post();
    return 0;
}

VEResult VEVideoRender::onInit(ANativeWindow * win) {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
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
    EGLConfig config;
    EGLint numConfigs;
    EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE
    };
    if (!eglChooseConfig(eglDisplay, configAttribs, &config, 1, &numConfigs)) {
        ALOGE("VEVideoRender::%s eglChooseConfig failed", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    // 创建 EGL 上下文
    EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3, // OpenGL ES 3.0
            EGL_NONE
    };
    eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext == EGL_NO_CONTEXT) {
        ALOGE("VEVideoRender::%s eglCreateContext failed", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    // 创建 EGL 表面
    eglSurface = eglCreateWindowSurface(eglDisplay, config, win, NULL);

    // 将 EGL 上下文与当前线程关联
    if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
        ALOGE("VEVideoRender::%s eglMakeCurrent failed", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    mProgram = createProgram(vertexShaderSource,fragmentShaderSource);
    createTexture();
    ALOGD("VEVideoRender::%s mViewWidth:%d,mViewHeight:%d", __FUNCTION__, mViewWidth, mViewHeight);
    glViewport(0,0,mViewWidth,mViewHeight);
    glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    return VE_OK;
}

VEResult VEVideoRender::onStart() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    mIsStarted = true;
    std::make_shared<AMessage>(kWhatSync,shared_from_this())->post();
    return VE_OK;
}

VEResult VEVideoRender::onStop() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    mIsStarted = false;
    return VE_OK;
}

VEResult VEVideoRender::onUnInit() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    return VE_OK;
}

VEResult VEVideoRender::onRender(std::shared_ptr<AMessage> msg) {
    ALOGI("VEVideoRender::%s enter",__FUNCTION__ );
    if(!mIsStarted){
        return UNKNOWN_ERROR;
    }

    int32_t isDrop = false;
    msg->findInt32("drop",&isDrop);

    if(isDrop){
        return VE_OK;
    }

    std::shared_ptr<VEFrame> frame = nullptr;
    std::shared_ptr<void> tmp;

    msg->findObject("render",&tmp);

    frame = std::static_pointer_cast<VEFrame>(tmp);

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(mProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width, frame->getFrame()->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width / 2, frame->getFrame()->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[1]);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mTextures[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frame->getFrame()->width / 2, frame->getFrame()->height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->getFrame()->data[2]);

    mFrameWidth = frame->getFrame()->width;
    mFrameHeight = frame->getFrame()->height;
    GLint positionLoc = glGetAttribLocation(mProgram, "aPosition");
    GLint texCoordLoc = glGetAttribLocation(mProgram, "aTexCoord");
    GLint transformLoc = glGetUniformLocation(mProgram, "u_TransformMatrix");

    GLint yTextureLoc = glGetUniformLocation(mProgram, "yTexture");
    GLint uTextureLoc = glGetUniformLocation(mProgram, "uTexture");
    GLint vTextureLoc = glGetUniformLocation(mProgram, "vTexture");

    glUniform1i(yTextureLoc, 0);
    glUniform1i(uTextureLoc, 1);
    glUniform1i(vTextureLoc, 2);

    ALOGD("VEVideoRender::%s ### mFrameWidth:%d,mFrameHeight:%d mViewWidth:%d,mViewHeight:%d pts:%" PRId64, __FUNCTION__, mFrameWidth, mFrameHeight, mViewWidth, mViewHeight,
          frame->getPts());
//    {
//        fwrite(frame->getFrame()->data[0],mFrameWidth* mFrameHeight,1,fp);
//        fwrite(frame->getFrame()->data[1],mFrameWidth* mFrameHeight/4,1,fp);
//        fwrite(frame->getFrame()->data[2],mFrameWidth* mFrameHeight/4,1,fp);
//        fflush(fp);
//    }

    float screenAspectRatio = (float)mViewWidth / mViewHeight;
    float imageAspectRatio = (float)mFrameWidth / mFrameHeight;
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
    float angle = 0.0f; // 旋转角度
    glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));

    // 使用glm::scale和glm::rotate函数创建一个同时包含缩放和旋转的变换矩阵
    glm::mat4 scaleRotationMatrix = glm::scale(rotationMatrix, scaleVector);

    glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(scaleRotationMatrix));

    glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
    glEnableVertexAttribArray(positionLoc);

    glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
    glEnableVertexAttribArray(texCoordLoc);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(eglDisplay,eglSurface);

    if(mNotify){
        std::shared_ptr<AMessage> msg = mNotify->dup();
        msg->setInt32("type",kWhatProgress);
        msg->setInt64("progress",static_cast<int64_t>(frame->getPts()));
        msg->post();
        ALOGI("VEVideoRender::%s - Notifying progress: %" PRId64 "  what:%d", __FUNCTION__,
              frame->getPts(), msg->what());
    }
    ALOGI("VEVideoRender::%s exit timestamp:%" PRId64,__FUNCTION__ , frame->getPts());
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
            char* infoLog = (char*)malloc(infoLen);
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
    if (GLenum error = glGetError() != GL_NO_ERROR) {
        ALOGE("VEVideoRender::%s Error attaching vertex shader: 0x%x", __FUNCTION__, error);
        glDeleteProgram(program);
        return 0;
    }
    glAttachShader(program, fragmentShader);
    if (GLenum error = glGetError() != GL_NO_ERROR) {
        ALOGE("VEVideoRender::%s Error attaching fragment shader: 0x%x", __FUNCTION__, error);
        glDeleteProgram(program);
        return 0;
    }

    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = (char*)malloc(infoLen);
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
    return 0;
}

VEResult VEVideoRender::pause() {
    std::make_shared<AMessage>(kWhatPause,shared_from_this())->post();
    return 0;
}

VEResult VEVideoRender::resume() {
    std::make_shared<AMessage>(kWhatResume,shared_from_this())->post();
    return 0;
}

VEResult VEVideoRender::onPause() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    return 0;
}

VEResult VEVideoRender::onResume() {
    ALOGI("VEVideoRender::%s",__FUNCTION__ );
    return 0;
}

VEResult VEVideoRender::onAVSync() {
    ALOGI("VEVideoRender::%s enter",__FUNCTION__ );
    if(!mIsStarted){
        return UNKNOWN_ERROR;
    }

    bool isDrop = false;
    std::shared_ptr<VEFrame> frame = nullptr;
    VEResult ret = mVDec->readFrame(frame);
    if(ret == VE_NOT_ENOUGH_DATA){
        ALOGI("VEVideoRender::%s needMoreFrame!!!",__FUNCTION__ );
        mVDec->needMoreFrame(std::make_shared<AMessage>(kWhatSync,shared_from_this()));
        return VE_NOT_ENOUGH_DATA;
    }

    if(frame == nullptr){
        ALOGE("VEVideoRender::%s onRender read frame is null!!!", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    if(frame->getFrameType() == E_FRAME_TYPE_EOF){
        ALOGD("VEVideoRender::onAVSync E_FRAME_TYPE_EOF");
        std::shared_ptr<AMessage> eosMsg = mNotify->dup();
        eosMsg->setInt32("type",kWhatEOS);
        eosMsg->post();
        return UNKNOWN_ERROR;
    }

    m_AVSync->updateVideoPts(frame->getPts());

    if (m_AVSync->shouldDropFrame()) {
        ALOGI("VEVideoRender::%s Dropping frame due to sync issues", __FUNCTION__);
        isDrop = true; // 丢帧
    }

    int64_t waitTime = m_AVSync->getWaitTime(); // 获取等待时间
    ALOGD("VEVideoRender::%s waitTime:%" PRId64,__FUNCTION__ ,waitTime);
    std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatRender, shared_from_this());
    renderMsg->setObject("render", frame);
    renderMsg->setInt32("drop",isDrop);
    renderMsg->post(isDrop ? 0 : waitTime); // 根据同步状态设置等待时间
    return VE_OK;
}
