#ifndef __VE_VIDEO_DECODER__
#define __VE_VIDEO_DECODER__

#include<memory>
#include <deque>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include"VEFrame.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEDemux.h"
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}


class VEVideoDecoder:public AHandler
{

public:
    VEVideoDecoder();
    ~VEVideoDecoder();

    int init(std::shared_ptr<VEDemux> demux);
    void start();
    void stop();
    int flush();
    int readFrame(std::shared_ptr<VEFrame> &frame);
    int uninit();

private:
    void queueFrame(std::shared_ptr<VEFrame> frame);
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    status_t onInit(std::shared_ptr<AMessage> msg);
    status_t onStart();
    status_t onStop();
    status_t onFlush();
    status_t onDecode();
    status_t onUninit();
    enum {
        kWhatInit                = 'init',
        kWhatStart               = 'star',
        kWhatStop                = 'stop',
        kWhatFlush               = 'flus',
        kWhatDecode              = 'deco',
        kWhatUninit              = 'unin'
    };
private:
    /* data */
    AVCodecContext * mVideoCtx=nullptr;
    std::shared_ptr<VEMediaInfo> mMediaInfo=nullptr;
    std::deque<std::shared_ptr<VEFrame>> mFrameQueue;
    std::shared_ptr<VEDemux> mDemux = nullptr;

    std::mutex mMutex;
    std::condition_variable mCond;

    bool mIsStoped = true;

    FILE *fp = nullptr;
};


#endif