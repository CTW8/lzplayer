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

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/timestamp.h"
}


class VEVideoDecoder
        : public AHandler
{
public:
    VEVideoDecoder();
    ~VEVideoDecoder();

    VEResult init(std::shared_ptr<VEDemux> demux);

    void start();
    void pause();
    void stop();
    VEResult flush();
    VEResult readFrame(std::shared_ptr<VEFrame> &frame);
    void needMoreFrame(std::shared_ptr<AMessage> msg);
    VEResult uninit();

protected:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

private:
    // 消息处理函数
    VEResult onInit(std::shared_ptr<AMessage> msg);
    VEResult onStart();
    VEResult onPause();
    VEResult onStop();
    VEResult onFlush();
    VEResult onDecode();
    VEResult onUninit();
    VEResult onNeedMoreFrame(const std::shared_ptr<AMessage> &msg);

    // 帧入队列
    void queueFrame(std::shared_ptr<VEFrame> frame);

    enum {
        kWhatInit   = 'init',
        kWhatStart  = 'star',
        kWhatStop   = 'stop',
        kWhatPause  = 'paus',
        kWhatResume = 'resu',  // 若需要resume可保留
        kWhatFlush  = 'flus',
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

#endif // __VE_VIDEO_DECODER__