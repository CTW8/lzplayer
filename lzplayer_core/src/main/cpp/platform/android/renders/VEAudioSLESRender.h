#ifndef __VE_AUDIO_SLES_RENDER__
#define __VE_AUDIO_SLES_RENDER__

#include "IAudioRender.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
namespace VE {
    class VEAudioSLESRender : public IAudioRender {
    public:
        VEAudioSLESRender();
        ~VEAudioSLESRender();

        VEResult configure(const AudioConfig &config) override;
        VEResult start() override;
        VEResult pause() override;
        VEResult flush() override;
        VEResult stop() override;
        VEResult renderFrame(std::shared_ptr<VEFrame> frame) override;
        VEResult release() override;

        friend void slesBufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

    private:
        // OpenSL ES 对象
        SLObjectItf mEngineObject;
        SLEngineItf mEngineEngine;
        SLObjectItf mOutputMixObject;
        SLObjectItf mPlayerObject;
        SLPlayItf mPlayerPlay;
        SLAndroidSimpleBufferQueueItf mBufferQueue;

        // 帧队列管理
        std::deque<std::shared_ptr<VEFrame>> m_FrameQueue;
        std::mutex m_Mutex;
        std::condition_variable m_Cond;

        // 配置和状态
        AudioConfig m_Config;
        bool m_IsConfigured;
        bool m_IsPlaying;

        // 缓冲区管理
        size_t m_BufferSize;
        uint8_t* m_TempBuffer;  // 静音缓冲区

        // 回调函数
        VEAudioCallback m_AudioCallback;

        // 常量定义
        static constexpr int BUFFER_QUEUE_SIZE = 2;        // 缓冲区队列大小
        static constexpr int MAX_FRAME_QUEUE_SIZE = 10;    // 最大帧队列大小
        static constexpr int BUFFER_DURATION_MS = 20;      // 缓冲区时长(毫秒)

        // 私有辅助函数
        void cleanup();

        SLuint32 convertSampleRate(int sampleRate);
        size_t calculateBufferSize(int sampleRate, int channels, int durationMs);
    };
}
#endif 