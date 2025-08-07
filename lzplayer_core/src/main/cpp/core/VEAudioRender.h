#ifndef __VE_AUDIO_RENDER__
#define __VE_AUDIO_RENDER__

#include "IAudioRender.h"
#include "VEAudioDecoder.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEAVsync.h"
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
namespace VE {
class VEAudioRender : public IVEComponent{
    public:
        VEAudioRender(const std::shared_ptr<AMessage> &notify,const std::shared_ptr<VEAVsync> &avSync);

        ~VEAudioRender();

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult stop() override;

        VEResult seekTo(double timestamp) override;

        VEResult flush() override;

        VEResult pause() override;

        VEResult release() override;
    enum {
        kWhatEOS = 'aeos',
        kWhatError = 'aerr'
    };


protected:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;


    private:
        VEResult onRender();

    private:
        std::shared_ptr<IAudioRender> m_AudioRenderer; // 音频渲染器接口
        std::shared_ptr<VEAudioDecoder> m_AudioDecoder;  // 音频解码器
        std::deque<std::shared_ptr<VEFrame>> m_FrameQueue; // PCM帧队列
        std::mutex m_Mutex;
        std::condition_variable m_Cond;
        std::shared_ptr<AMessage> m_Notify = nullptr;

        std::shared_ptr<VEAVsync> m_AVSync = nullptr;
        uint8_t * mSliceBuffer = nullptr;

        FILE *fp= nullptr;

        // 消息类型
        enum {
            kWhatPrepare = 'prep',
            kWhatStart = 'star',
            kWhatPause = 'paus',
            kWhatStop = 'stop',
            kWhatSeek = 'seek',
            kWhatRender = 'rend',
            kWhatFlush = 'flus',
            kWhatRelease = 'rele',
        };
    };
}
#endif