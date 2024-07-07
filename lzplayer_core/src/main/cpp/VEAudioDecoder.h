#ifndef __VE_AUDIO_DECODER__
#define __VE_AUDIO_DECODER__

#include<string>
#include<memory>

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
    #include "libswresample/swresample.h"
    #include "libavutil/opt.h"
}

#include"VEMediaDef.h"
#include"VEPacket.h"
#include"VEFrame.h"
#include "VEDemux.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"

class VEAudioDecoder:public AHandler{
public:
    VEAudioDecoder();
    ~VEAudioDecoder();

public:
    ///init
    int init(std::shared_ptr<VEDemux> demux);
    void start();
    void stop();
    int flush();
    int readFrame(std::shared_ptr<VEFrame> &frame);
    int uninit();

private:
    bool onInit(std::shared_ptr<AMessage> msg);
    bool onStart();
    bool onFlush();
    bool onDecode();
    bool onStop();
    bool onUnInit();
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
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
    AVCodecContext * mAudioCtx=nullptr;
    VEMediaInfo * mMediaInfo=nullptr;
    std::mutex mMutex;
    std::condition_variable mCond;
    std::deque<std::shared_ptr<VEFrame>> mFrameQueue;
    std::shared_ptr<VEDemux> mDemux = nullptr;
    bool mIsStarted = false;

    SwrContext *mSwrCtx = nullptr;
};

#endif