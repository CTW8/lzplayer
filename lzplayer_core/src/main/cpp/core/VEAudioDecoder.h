#ifndef __VE_AUDIO_DECODER__
#define __VE_AUDIO_DECODER__

#include <string>
#include <memory>
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEDemux.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "IFrameSink.h"
#include "IMediaDecoder.h"
#include "VEAudioOutputConfig.h"

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
    #include "libswresample/swresample.h"
    #include "libavutil/opt.h"
}
namespace VE {
    class VEAudioDecoder : public AHandler, public IMediaDecoder{
    public:
        void setPerfStats(const std::shared_ptr<VEPerfStats> &stats) override {
            mPerfStats = stats;
        }

        VEAudioDecoder(std::shared_ptr<AMessage> &notify);

        ~VEAudioDecoder() override;

        /// IMediaDecoder：params 需带 outSampleRate/outChannels/outFormat
        /// (由播放器统一决定，解码器按它重采样，与渲染器设备参数同源)；
        /// sink 是解出的帧的去向(音频渲染)，推模型 + 消费回执 credit
        VEResult prepare(std::shared_ptr<IMediaSource> source,
                         std::shared_ptr<IFrameSink> sink,
                         const VEBundle &params) override;

        /// 便捷重载：内部包装成 VEBundle 后转调上面那个
        VEResult prepare(std::shared_ptr<IMediaSource> source,
                         const VEAudioOutputConfig &outConfig,
                         std::shared_ptr<IFrameSink> sink);

        // IVEComponent：命令面，VEPlayer 按 Role 表统一扇出
        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult flush() override;

        VEResult release() override;

        VEResult seekTo(double timestamp) override;

    private:
        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart();

        VEResult onPause();

        VEResult onStop();

        VEResult onFlush();

        VEResult onDecode();

        VEResult onRelease();

        VEResult onSeek(double timestampMs);

        /// 投递带当前 epoch 的解码消息，flush 后旧消息会被自动丢弃。
        /// delayUs>0 用于上游饥饿时的轮询重试
        void postDecode(int64_t delayUs = 0);

        /// 推一帧给 sink：附带回执消息(带当前 epoch)，在途帧计数 +1
        void queueFrame(std::shared_ptr<VEFrame> frame);

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);
        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatResume = 'resu',
            kWhatSeek   = 'seek',
            kWhatFlush = 'flus',
            kWhatDecode = 'deco',
            kWhatUninit = 'unin',
            kWhatFrameConsumed = 'fcon'
        };

    private:
        /// mSeekTargetUs 的哨兵值：当前没有待完成的精准 seek
        static const int64_t kNoSeekTarget = INT64_MIN;

        AVCodecContext *mAudioCtx = nullptr;
        /// 持有源的媒体信息：轨道的 codecParams 归它所有，
        /// 解码器存活期间必须一并持住，避免源释放后悬垂
        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
        std::shared_ptr<IMediaSource> mDemux = nullptr;
        /// 帧的去向；credit 流控由 mInFlightFrames 表达
        std::shared_ptr<IFrameSink> mSink = nullptr;
        /// 已推给 sink 但还没收到消费回执的帧数(仅解码 looper 访问)
        int mInFlightFrames = 0;

        std::shared_ptr<AMessage> mNotifyEvent = nullptr;

        /// 稳态指标，由 VEPlayer 注入。只在本 looper 上写

        std::shared_ptr<VEPerfStats> mPerfStats;
        /// 已送包但还没结算到某一帧上的解码耗时(微秒)。同 VEVideoDecoder
        int64_t mDecodeAccumUs = 0;
        /// 同 mDecodeAccumUs, 但记的是 CPU 时间(自校验用)
        int64_t mDecodeAccumCpuUs = 0;
        /// 本次饥饿的起点(微秒)，0=当前不处于饥饿。用于算饥饿持续时长
        int64_t mStarveBeginUs = 0;

        bool mIsStarted = false;

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
        bool mIsEOS = false;
        SwrContext *mSwrCtx = nullptr;

        /// 连续送入失败(坏包)计数，成功即清零；超上限才按致命错误处理。
        /// 与 demux 的坏包容忍策略对齐，避免单个损坏包打死整个播放。
        int mSendErrorCount = 0;

        /// 重采样目标，prepare 时由播放器下发
        int32_t mOutSampleRate = 44100;
        int32_t mOutChannels = 2;
        int32_t mOutFormat = AV_SAMPLE_FMT_S16;
    };
}

#endif