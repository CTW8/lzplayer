#ifndef __I_AUDIO_RENDERER__
#define __I_AUDIO_RENDERER__

#include <memory>
#include "VEFrame.h"

// 定义音频配置结构体
struct AudioConfig {
    int sampleRate;      // 采样率
    int channels;        // 通道数
    int sampleFormat;    // 采样格式
};

class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;

    // 初始化音频渲染器
    virtual int init() = 0;

    // 配置音频渲染器参数
    virtual int configure(const AudioConfig &config) = 0;

    // 开始音频渲染
    virtual void start() = 0;

    // 停止音频渲染
    virtual void stop() = 0;

    // 渲染音频帧
    virtual void renderFrame(std::shared_ptr<VEFrame> frame) = 0;

    // 释放音频渲染器资源
    virtual int uninit() = 0;
};

#endif 