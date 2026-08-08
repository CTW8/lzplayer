#ifndef __VE_AUDIO_RENDER__
#define __VE_AUDIO_RENDER__

#include "IAudioRender.h"
#include "utils/VEStartupTrace.h"
#include "IFrameSink.h"
#include "IVEComponent.h"
#include "VEAudioOutputConfig.h"
#include "VESonicProcessor.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEAVsync.h"
#include <memory>
#include <deque>
#include <mutex>
#include <atomic>
#include <condition_variable>
namespace VE {
class VEAudioRender : public AHandler, public IFrameSink, public IVEComponent{
    public:
        VEAudioRender(const std::shared_ptr<AMessage> &notify,const std::shared_ptr<VEAVsync> &avSync);

        ~VEAudioRender() override;

        /// 推模型下渲染器不再持有解码器：帧由解码器 queueFrame 推进来
        VEResult prepare(const VEAudioOutputConfig &config);

        /// IFrameSink：解码器线程调用，转投自己的 looper(盖当前队列代次)
        void queueFrame(const std::shared_ptr<VEFrame> &frame,
                        const std::shared_ptr<AMessage> &consumedReply) override;

        // IVEComponent：命令面，VEPlayer 按 Role 表统一扇出
        VEResult start() override;

        VEResult stop() override;

        VEResult seekTo(double timestamp) override;

        VEResult flush() override;

        VEResult pause() override;

        VEResult release() override;

        /// 由 OpenSL ES 回调线程调用：投递一条带当前代次的渲染消息
        void postRender(int64_t delayUs = 0);

        /// 设置播放速率(0.5~2.0，音调不变)。异步执行：切换时会 flush
        /// 变速器与设备缓冲，并把时钟记账重新锚定到当前位置。
        VEResult setSpeed(float speed, double anchorPtsUs);

        /// 强制使用 OpenSL ES(测试用)。必须在 prepare 之前调用。
        void setForceSles(bool force) { m_ForceSles = force; }

        /// 注入启播里程碑记录器(T8 首个音频帧进设备时打点)
        void setStartupTrace(const std::shared_ptr<VEStartupTrace> &trace) {
            mStartupTrace = trace;
        }

        /// 实际生效的后端名，诊断面板显示用。prepare 之后才有意义。
        const char *backendName() const { return m_BackendName.load(); }

    enum {
        kWhatEOS = 'aeos',
        kWhatError = 'aerr'
    };


protected:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;


private:
        VEResult onRender();

        /// 1.0x 直通路径：帧原样喂设备
        VEResult renderPassthrough();

        /// 变速路径：帧先过 sonic，再按设备块喂出去
        VEResult renderTimeStretched();

        /// 用 staging 里的变速结果造一帧可交给设备的 PCM
        std::shared_ptr<VEFrame> makeStretchedFrame(int samplesPerChannel);

        /// 按"已送出媒体位置 - 设备内未播出量"给主时钟打点。
        /// mediaPosUs 是已入队数据末端对应的媒体时间。
        void anchorClock(double mediaPosUs);

        /// 变速器与记账清零(seek/flush/stop/变速切换都要做)
        void resetStretchState();

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
        /// 测试开关：强制走 SLES(prepare 前设置)
        bool m_ForceSles = false;
        /// 启播里程碑，由 VEPlayer 注入
        std::shared_ptr<VEStartupTrace> mStartupTrace;
        /// 实际生效的后端名(prepare 时写一次，之后只读)
        std::atomic<const char *> m_BackendName{"none"};
        /// 设备侧硬错误闩：置位后停止喂帧，等播放器收敛。
        /// stop/flush/seek/release 会清掉它(设备重建后可再试)
        bool m_DeviceFailed = false;

        // ---- 变速(time-stretch) ----
        /// 播放速率。1.0 时完全旁路 sonic，零开销
        float m_Speed = 1.0f;
        VESonicProcessor mSonic;
        /// 设备输出参数(prepare 时确定)，变速记账要按它折算时间
        int m_OutSampleRate = 0;
        int m_OutChannels = 0;
        int m_OutFormat = 0;
        /// 一个设备块的每声道样本数(按 20ms 粒度，与 SLES 缓冲对齐)
        int m_BlockSamples = 0;
        /// 变速记账的锚点：flush 之后第一帧输入的 pts(微秒)。
        /// kNoAnchor 表示还没喂过数据。
        static constexpr int64_t kNoAnchor = INT64_MIN;
        int64_t m_AnchorPtsUs = kNoAnchor;
        /// 自锚点起从 sonic 读出并送进设备的累计样本数(每声道)
        int64_t m_OutSamplesTotal = 0;
        /// 设备满时暂存的一块变速输出，下轮优先补喂
        std::shared_ptr<VEFrame> m_PendingOut = nullptr;
        /// 已经吞掉 EOF 输入、正在把 sonic 尾巴冲出来
        bool m_Draining = false;
        /// 渲染消息代次；由 SLES 回调线程读取，故用原子量
        std::atomic<int32_t> m_Epoch{0};

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
            kWhatSetSpeed = 'sspd',
        };
    };
}
#endif