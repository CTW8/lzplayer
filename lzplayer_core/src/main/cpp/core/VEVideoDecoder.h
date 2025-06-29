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
            : public AHandler ,IVEComponent{
    public:
        VEVideoDecoder();

    public:
        ~VEVideoDecoder();

        VEResult prepare(std::shared_ptr<VEDemux> demux);

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult flush() override;

        VEResult seekTo(uint64_t timestamp) override;

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

        // 帧入队列
        void queueFrame(std::shared_ptr<VEFrame> frame);

        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatFlush = 'flus',
            kWhatDecode = 'deco',
            kWhatUninit = 'unin',
            kWhatNeedMore = 'need'
        };

    private:
        AVCodecContext *mVideoCtx = nullptr;
        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
        std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
        std::shared_ptr<VEDemux> mDemux = nullptr;

        std::mutex mMutex;               // 保护共享变量
        bool mIsStarted = false;
        bool mNeedMoreData = false;

        std::shared_ptr<AMessage> mNotifyMore = nullptr;
    };
}
#endif // __VE_VIDEO_DECODER__