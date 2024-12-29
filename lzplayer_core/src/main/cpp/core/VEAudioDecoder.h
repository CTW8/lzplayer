#ifndef __VE_AUDIO_DECODER__
#define __VE_AUDIO_DECODER__

#include <string>
#include <memory>
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEFrameQueue.h"
#include "VEDemux.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
    #include "libswresample/swresample.h"
    #include "libavutil/opt.h"
}

class VEAudioDecoder : public AHandler {
public:
    VEAudioDecoder();
    ~VEAudioDecoder();

    VEResult init(std::shared_ptr<VEDemux> demux);
    void start();
    void pause();
    void resume();
    void stop();
    VEResult flush();
    void needMoreFrame(std::shared_ptr<AMessage> msg);
    VEResult readFrame(std::shared_ptr<VEFrame> &frame);
    VEResult uninit();

private:
    VEResult onInit(std::shared_ptr<AMessage> msg);
    VEResult onStart();
    VEResult onFlush();
    VEResult onPause();
    VEResult onResume();
    VEResult onDecode();
    VEResult onStop();
    VEResult onUnInit();
    void queueFrame(std::shared_ptr<VEFrame> frame);
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

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
    AVCodecContext *mAudioCtx = nullptr;
    VEMediaInfo *mMediaInfo = nullptr;
    std::shared_ptr<VEFrameQueue> mFrameQueue= nullptr;
    std::shared_ptr<VEDemux> mDemux = nullptr;

    std::mutex mMutex;
    bool mIsStarted = false;
    bool mNeedMoreData = false;

    std::shared_ptr<AMessage> mNotifyRender = nullptr;
    SwrContext *mSwrCtx = nullptr;
    FILE *fp = nullptr;
};

#endif