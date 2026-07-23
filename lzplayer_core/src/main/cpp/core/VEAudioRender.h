#ifndef __VE_AUDIO_RENDER__
#define __VE_AUDIO_RENDER__

#include "IAudioRender.h"
#include "IFrameSink.h"
#include "VEAudioOutputConfig.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEAVsync.h"
#include <memory>
#include <deque>
#include <mutex>
#include <atomic>
#include <condition_variable>
namespace VE {
class VEAudioRender : public AHandler, public IFrameSink{
    public:
        VEAudioRender(const std::shared_ptr<AMessage> &notify,const std::shared_ptr<VEAVsync> &avSync);

        ~VEAudioRender() override;

        /// 推模型下渲染器不再持有解码器：帧由解码器 queueFrame 推进来
        VEResult prepare(const VEAudioOutputConfig &config);

        /// IFrameSink：解码器线程调用，转投自己的 looper(盖当前队列代次)
        void queueFrame(const std::shared_ptr<VEFrame> &frame,
                        const std::shared_ptr<AMessage> &consumedReply) override;

        // 生命周期命令由 VEPlayer 持具体类型直接调用，不再经接口
        VEResult start();

        VEResult stop();

        VEResult seekTo(double timestamp);

        VEResult flush();

        VEResult pause();

        VEResult release();

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
        /// 本地帧队列(帧 + 它的消费回执)。只在本 looper 上访问，无需加锁。
        /// 队列空 ⟺ 喂帧链停摆，帧到达即重新拉起(SLES 回调只负责继续消费)。
        std::deque<std::pair<std::shared_ptr<VEFrame>,
                             std::shared_ptr<AMessage>>> mFrames;
        /// 队列代次：解码器线程投递时盖章(atomic 读)，flush/seek/stop 递增
        std::atomic<int32_t> mQueueGen{0};
        std::shared_ptr<AMessage> m_Notify = nullptr;

        std::shared_ptr<VEAVsync> m_AVSync = nullptr;

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
            kWhatQueueFrame = 'qfrm',
        };
    };
}
#endif