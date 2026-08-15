#ifndef __VE_VIDEO_DECODER__
#define __VE_VIDEO_DECODER__

#include <memory>
#include <mutex>
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEDemux.h"
#include "IFrameSink.h"
#include "IMediaDecoder.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/timestamp.h"
#include "libswscale/swscale.h"
}

namespace VE {
    class VEVideoDecoder
            : public AHandler, public IMediaDecoder{
    public:
        VEVideoDecoder(std::shared_ptr<AMessage> &nofity);
        ~VEVideoDecoder() override;

        /// IMediaDecoder：sink 是解出的帧的去向(视频显示)，
        /// 推模型 + 消费回执 credit。软解不使用 params。
        void setStartupTrace(const std::shared_ptr<VEStartupTrace> &trace) override {
            mStartupTrace = trace;
        }

        void setPerfStats(const std::shared_ptr<VEPerfStats> &stats) override {
            mPerfStats = stats;
        }

        VEResult prepare(std::shared_ptr<IMediaSource> source,
                         std::shared_ptr<IFrameSink> sink,
                         const VEBundle &params) override;

        // IVEComponent：命令面，VEPlayer 按 Role 表统一扇出
        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult flush() override;

        VEResult seekTo(double timestampMs) override;

        VEResult release() override;

    protected:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

    private:
        // 消息处理函数
        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart();

        VEResult onPause();

        VEResult onStop();

        VEResult onFlush();

        VEResult onDecode();

        VEResult onRelease();

        VEResult onSeek(double timestampMs);

        /// 把渲染器不支持的像素格式转成 YUV420P
        std::shared_ptr<VEFrame> convertToYuv420p(const std::shared_ptr<VEFrame> &src);

        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);

        /// 投递带当前 epoch 的解码消息，flush 后旧消息会被自动丢弃
        void postDecode(int64_t delayUs = 0);

        /// 推一帧给 sink：附带回执消息(带当前 epoch)，在途帧计数 +1
        void queueFrame(std::shared_ptr<VEFrame> frame);

        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatFlush = 'flus',
            kWhatSeek  = 'seek',
            kWhatDecode = 'deco',
            kWhatUninit = 'unin',
            kWhatFrameConsumed = 'fcon'
        };

    private:
        /// mSeekTargetUs 的哨兵值：当前没有待完成的精准 seek
        static const int64_t kNoSeekTarget = INT64_MIN;

        AVCodecContext *mVideoCtx = nullptr;
        /// 仅在解码输出不是 YUV420P 时才会创建
        SwsContext *mSwsCtx = nullptr;
        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
        /// 只依赖数据源接口：本地 demux 与后续的网络源可以互换
        std::shared_ptr<IMediaSource> mDemux = nullptr;
        /// 帧的去向；credit 流控由 mInFlightFrames 表达
        std::shared_ptr<IFrameSink> mSink = nullptr;
        /// 已推给 sink 但还没收到消费回执的帧数(仅解码 looper 访问)
        int mInFlightFrames = 0;
        bool mIsEOS = false;

        std::shared_ptr<AMessage> mNofityEvent = nullptr;

        bool mIsStarted = false;
        /// 启播里程碑，由 VEPlayer 注入。只在解码器自身 looper 上访问
        std::shared_ptr<VEStartupTrace> mStartupTrace;
        /// 稳态指标，由 VEPlayer 注入。只在本 looper 上写，无需加锁
        std::shared_ptr<VEPerfStats> mPerfStats;
        /// 已送包但还没结算到某一帧上的解码耗时(微秒)。见 onDecode 注释
        int64_t mDecodeAccumUs = 0;
        /// 同 mDecodeAccumUs, 但记的是 CPU 时间(自校验用)
        int64_t mDecodeAccumCpuUs = 0;
        /// 本次饥饿的起点(微秒)，0=当前不处于饥饿。用于算饥饿持续时长
        int64_t mStarveBeginUs = 0;

        /// 解码消息的代次，flush/seek 时递增以作废在途的解码消息
        /// 饥饿唤醒代次。一次饥饿会同时武装**两个**唤醒源(数据到达通知 +
        /// 兜底重试)，常见情况下两者都会触发：通知在几十毫秒后拉起链路，
        /// 兜底消息在 500ms 时又拉起一条。而 kWhatDecode 成功后会自我续投，
        /// 于是变成两条并行的自驱循环——两条消息 epoch 相同，互相过滤不掉。
        ///
        /// 用代次让它们互斥：两个唤醒源带同一个 gen，谁先到谁生效并递增 gen，
        /// 另一个到达时代次已过期直接丢弃。这与 epoch / queueGen 是同一个套路。
        ///
        /// (credit 复活那条路径本身是边沿触发的，不需要这层保护)
        int32_t mStarveGen = 0;
        int32_t mEpoch = 0;
        /// 精准 seek 目标(微秒)，其之前解出的帧全部丢弃
        int64_t mSeekTargetUs = kNoSeekTarget;
        /// 连续送入失败(坏包)计数，成功即清零；超上限才按致命错误处理。
        /// 与 demux 的坏包容忍策略对齐，避免单个损坏包打死整个播放。
        int mSendErrorCount = 0;
    };
}
#endif // __VE_VIDEO_DECODER__