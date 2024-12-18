#ifndef __VE_AUDIO_SLES_RENDER__
#define __VE_AUDIO_SLES_RENDER__

#include "IAudioRenderer.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>

class VEAudioSLESRender : public IAudioRenderer {
public:
    VEAudioSLESRender();
    ~VEAudioSLESRender();

    // 初始化音频渲染器
    int init() override;

    // 配置音频渲染器参数
    int configure(const AudioConfig &config) override;

    // 开始音频渲染
    void start() override;

    // 停止音频渲染
    void stop() override;

    // 渲染音频帧
    void renderFrame(std::shared_ptr<VEFrame> frame) override;

    // 释放音频渲染器资源
    int uninit() override;

private:
    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;
    SLObjectItf playerObject;
    SLPlayItf playerPlay;
    SLAndroidSimpleBufferQueueItf bufferQueue;

    std::deque<std::shared_ptr<VEFrame>> m_FrameQueue;
    std::mutex m_Mutex;
    std::condition_variable m_Cond;

    AudioConfig m_Config; // 存储音频配置

    // 初始化OpenSL ES
    int initOpenSLES();

    // 播放回调
    static void playerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);
};

#endif 