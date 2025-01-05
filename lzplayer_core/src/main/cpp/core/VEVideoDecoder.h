#ifndef __VE_VIDEO_DECODER__
#define __VE_VIDEO_DECODER__

#include <memory>
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

class VEVideoDecoder : public AHandler {
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

private:
    void queueFrame(std::shared_ptr<VEFrame> frame);
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    VEResult onInit(std::shared_ptr<AMessage> msg);
    VEResult onStart();
    VEResult onPause();
    VEResult onStop();
    VEResult onFlush();
    VEResult onDecode();
    VEResult onUninit();

    enum {
        kWhatInit = 'init',
        kWhatStart = 'star',
        kWhatStop = 'stop',
        kWhatPause = 'paus',
        kWhatResume = 'resu',
        kWhatFlush = 'flus',
        kWhatDecode = 'deco',
        kWhatUninit = 'unin'
    };

private:
    AVCodecContext *mVideoCtx = nullptr;
    std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
    std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
    std::shared_ptr<VEDemux> mDemux = nullptr;

    std::mutex mMutex;
    std::condition_variable mCond;

    bool mIsStarted = false;
    bool mNeedMoreData = false;

    std::shared_ptr<AMessage> mNotifyMore = nullptr;

    FILE *fp = nullptr;
};

#endif