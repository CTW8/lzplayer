#ifndef __VE_VIDEO_DECODER__
#define __VE_VIDEO_DECODER__

#include<memory>
#include <deque>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include"VEFrame.h"
#include "AHandler.h"
#include "AMessage.h"
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
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    bool onInit(std::shared_ptr<AMessage> msg);
    bool onStart();
    bool onStop();
    bool onFlush();
    bool onDecode();
    bool onUninit();
    enum {
        kWhatInit                = 'init',
        kWhatStart               = 'star',
        kWhatStop                = 'stop',
        kWhatFlush               = 'flus',
        kWhatRead                = 'read',
        kWhatUninit              = 'unin'
    };
private:
    /* data */
    AVCodecContext * mVideoCtx=nullptr;
    VEMediaInfo * mMediaInfo=nullptr;
    std::deque<std::shared_ptr<VEFrame>> mFrameQueue;
    std::shared_ptr<VEDemux> mDemux = nullptr;

    std::mutex mMutex;
    std::condition_variable mCond;
};


#endif