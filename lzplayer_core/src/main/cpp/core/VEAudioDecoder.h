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
#include "VEBundle.h"

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
    #include "libswresample/swresample.h"
    #include "libavutil/opt.h"
}
namespace VE {
    class VEAudioDecoder : public AHandler{
    public:
        VEAudioDecoder(std::shared_ptr<AMessage> &notify);

        ~VEAudioDecoder();

        VEResult prepare(std::shared_ptr<VEDemux> demux);

        VEResult start();

        VEResult pause();

        VEResult stop();

        VEResult flush();

        VEResult release();

        VEResult prepare(VEBundle params);

        VEResult seekTo(double timestamp);

        void needMoreFrame(std::shared_ptr<AMessage> msg);

        VEResult readFrame(std::shared_ptr<VEFrame> &frame);

    private:
        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart();

        VEResult onPause();

        VEResult onStop();

        VEResult onFlush();

        VEResult onDecode();

        VEResult onRelease();

        VEResult onSeek(double timestamp);

        VEResult onNeedMoreFrame(const std::shared_ptr<AMessage> &msg);

        void queueFrame(std::shared_ptr<VEFrame> frame);

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);
        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatResume = 'resu',
            kWhatSeek   = 'seek',
            kWhatFlush = 'flus',
            kWhatDecode = 'deco',
            kWhatUninit = 'unin',
            kWhatNeedMore = 'need'
        };

    private:
        AVCodecContext *mAudioCtx = nullptr;
        VEMediaInfo *mMediaInfo = nullptr;
        std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
        std::shared_ptr<VEDemux> mDemux = nullptr;

        std::shared_ptr<AMessage> mNotifyEvent = nullptr;

        std::mutex mMutex;
        bool mIsStarted = false;
        bool mNeedMoreData = false;

        std::shared_ptr<AMessage> mNotifyMore = nullptr;
        bool mIsEOS = false;
        SwrContext *mSwrCtx = nullptr;
    };
}

#endif