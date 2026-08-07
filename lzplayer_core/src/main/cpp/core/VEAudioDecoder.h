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

        bool mIsStarted = false;

        /// 解码消息的代次，flush/seek 时递增以作废在途的解码消息
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