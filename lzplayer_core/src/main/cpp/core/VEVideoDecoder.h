#ifndef __VE_VIDEO_DECODER__
#define __VE_VIDEO_DECODER__

#include <memory>
#include <mutex>
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEFrameQueue.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEDemux.h"
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

        VEResult prepare(std::shared_ptr<IMediaSource> source);

        // 生命周期命令由 VEPlayer 持具体类型直接调用，不再经接口
        VEResult start();

        VEResult pause();

        VEResult stop();

        VEResult flush();

        VEResult seekTo(double timestampMs);

        VEResult readFrame(std::shared_ptr<VEFrame> &frame) override;

        void needMoreFrame(std::shared_ptr<AMessage> msg) override;

        VEResult release();

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

        VEResult onNeedMoreFrame(const std::shared_ptr<AMessage> &msg);

        VEResult onSeek(double timestampMs);

        /// 把渲染器不支持的像素格式转成 YUV420P
        std::shared_ptr<VEFrame> convertToYuv420p(const std::shared_ptr<VEFrame> &src);

        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);

        /// 投递带当前 epoch 的解码消息，flush 后旧消息会被自动丢弃
        void postDecode(int64_t delayUs = 0);

        // 帧入队列
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
            kWhatNeedMore = 'need'
        };

    private:
        /// mSeekTargetUs 的哨兵值：当前没有待完成的精准 seek
        static const int64_t kNoSeekTarget = INT64_MIN;

        AVCodecContext *mVideoCtx = nullptr;
        /// 仅在解码输出不是 YUV420P 时才会创建
        SwsContext *mSwsCtx = nullptr;
        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
        std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
        /// 只依赖数据源接口：本地 demux 与后续的网络源可以互换
        std::shared_ptr<IMediaSource> mDemux = nullptr;
        bool mIsEOS = false;

        std::shared_ptr<AMessage> mNofityEvent = nullptr;

        bool mIsStarted = false;
        bool mNeedMoreData = false;

        /// 解码消息的代次，flush/seek 时递增以作废在途的解码消息
        int32_t mEpoch = 0;
        /// 精准 seek 目标(微秒)，其之前解出的帧全部丢弃
        int64_t mSeekTargetUs = kNoSeekTarget;

        std::shared_ptr<AMessage> mNotifyMore = nullptr;
    };
}
#endif // __VE_VIDEO_DECODER__