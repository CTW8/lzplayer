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

        /// 解码消息的代次，flush/seek 时递增以作废在途的解码消息
        int32_t mEpoch = 0;
        /// 精准 seek 目标(微秒)，其之前解出的帧全部丢弃
        int64_t mSeekTargetUs = kNoSeekTarget;
        /// 连续送入失败(坏包)计数，成功即清零；超上限才按致命错误处理。
        /// 与 demux 的坏包容忍策略对齐，避免单个损坏包打死整个播放。
        int mSendErrorCount = 0;
    };
}
#endif // __VE_VIDEO_DECODER__