#ifndef __I_AUDIO_RENDERER__
#define __I_AUDIO_RENDERER__

#include <memory>
#include "core/VEFrame.h"
#include "VEError.h"

namespace VE {
    typedef std::function<int()> VEAudioCallback;
// 定义音频配置结构体
    struct AudioConfig {
        int sampleRate;      // 采样率
        int channels;        // 通道数
        int sampleFormat;    // 采样格式
        VEAudioCallback onCallback;
    };

    class IAudioRender {
    public:
        virtual ~IAudioRender() = default;

        // 配置音频渲染器参数
        virtual VEResult configure(const AudioConfig &config) = 0;

        // 开始音频渲染
        virtual VEResult start() = 0;

        virtual VEResult pause() =0;

        virtual VEResult flush() =0;

        // 停止音频渲染
        virtual VEResult stop() = 0;

        // 渲染音频帧
        virtual VEResult renderFrame(std::shared_ptr<VEFrame> frame) = 0;

        // 释放音频渲染器资源
        virtual VEResult release() = 0;
    };
}
#endif 