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
#include "IVEComponent.h"
#include "VEBundle.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/timestamp.h"
}

namespace VE {
    class VEVideoDecoder
            : public IVEComponent{
    public:
        VEVideoDecoder(std::shared_ptr<AMessage> &nofity);
        ~VEVideoDecoder() override;

        VEResult prepare(std::shared_ptr<VEDemux> demux);

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult flush() override;

        VEResult seekTo(double timestampMs) override;

        VEResult readFrame(std::shared_ptr<VEFrame> &frame);

        void needMoreFrame(std::shared_ptr<AMessage> msg);

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

        VEResult onNeedMoreFrame(const std::shared_ptr<AMessage> &msg);

        VEResult onSeek(double timestampMs);

        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);

        /// 投递带当前 epoch 的解码消息，flush 后旧消息会被自动丢弃
        void postDecode();

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
        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
        std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
        std::shared_ptr<VEDemux> mDemux = nullptr;
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