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
#include "IVEComponent.h"
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
    class VEAudioDecoder : public IVEComponent{
    public:
        VEAudioDecoder();

        ~VEAudioDecoder();

        VEResult prepare(std::shared_ptr<VEDemux> demux);

        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult flush() override;

        VEResult release() override;

        VEResult prepare(VEBundle params) override;

        VEResult seekTo(double timestamp) override;

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

        VEResult onNeedMoreFrame(const std::shared_ptr<AMessage> &msg);

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
            kWhatUninit = 'unin',
            kWhatNeedMore = 'need'
        };

    private:
        AVCodecContext *mAudioCtx = nullptr;
        VEMediaInfo *mMediaInfo = nullptr;
        std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
        std::shared_ptr<VEDemux> mDemux = nullptr;

        std::mutex mMutex;
        bool mIsStarted = false;
        bool mNeedMoreData = false;

        std::shared_ptr<AMessage> mNotifyMore = nullptr;
        bool mIsEOS = false;
        SwrContext *mSwrCtx = nullptr;
    };
}

#endif