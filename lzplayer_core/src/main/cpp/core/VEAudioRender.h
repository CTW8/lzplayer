#ifndef __VE_AUDIO_RENDER__
#define __VE_AUDIO_RENDER__

#include "IAudioRender.h"
#include "IMediaDecoder.h"
#include "VEAudioDecoder.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEAVsync.h"
#include <memory>
#include <deque>
#include <mutex>
#include <atomic>
#include <condition_variable>
namespace VE {
class VEAudioRender : public IVEComponent{
    public:
        VEAudioRender(const std::shared_ptr<AMessage> &notify,const std::shared_ptr<VEAVsync> &avSync);

        ~VEAudioRender() override;

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult stop() override;

        VEResult seekTo(double timestamp) override;

        VEResult flush() override;

        VEResult pause() override;

        VEResult release() override;

        /// 由 OpenSL ES 回调线程调用：投递一条带当前代次的渲染消息
        void postRender(int64_t delayUs = 0);

    enum {
        kWhatEOS = 'aeos',
        kWhatError = 'aerr'
    };


protected:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;


private:
        VEResult onRender();
    VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);

    private:
        std::shared_ptr<IAudioRender> m_AudioRenderer; // 音频渲染器接口
        /// 只依赖解码器接口，不绑定具体实现
        std::shared_ptr<IMediaDecoder> m_AudioDecoder;
        std::deque<std::shared_ptr<VEFrame>> m_FrameQueue; // PCM帧队列
        std::mutex m_Mutex;
        std::condition_variable m_Cond;
        std::shared_ptr<AMessage> m_Notify = nullptr;

        std::shared_ptr<VEAVsync> m_AVSync = nullptr;
        uint8_t * mSliceBuffer = nullptr;

        bool m_IsStarted = false;
        /// 渲染消息代次；由 SLES 回调线程读取，故用原子量
        std::atomic<int32_t> m_Epoch{0};

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