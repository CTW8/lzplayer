#ifndef __VE_AUDIO_RENDER__
#define __VE_AUDIO_RENDER__

#include "IAudioRender.h"
#include "VEAudioDecoder.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
namespace VE {
    class VEAudioRender : public AHandler ,IVEComponent{
    public:
        VEAudioRender(const std::shared_ptr<IAudioRender>& audioRenderer,
                      const std::shared_ptr<VEAudioDecoder>& audioDecoder);

        ~VEAudioRender();

        VEResult prepare(const AudioConfig &config);

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult stop() override;

        VEResult seekTo(uint64_t timestamp) override;

        VEResult flush() override;

        VEResult pause() override;

        VEResult release() override;

    public:
        // 渲染音频帧
        void renderFrame(std::shared_ptr<VEFrame> frame);

    protected:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

    private:
        std::shared_ptr<IAudioRender> m_AudioRenderer; // 音频渲染器接口
        std::shared_ptr<VEAudioDecoder> m_AudioDecoder;  // 音频解码器
        std::deque<std::shared_ptr<VEFrame>> m_FrameQueue; // PCM帧队列
        std::mutex m_Mutex;
        std::condition_variable m_Cond;

        // 声明 fetchAudioData 函数
        void fetchAudioData();

        // 消息类型
        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatRender = 'rend',
            kWhatFetchData = 'fdat',
        };
    };
}
#endif