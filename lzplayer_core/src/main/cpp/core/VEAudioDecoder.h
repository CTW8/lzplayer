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
#include "IMediaDecoder.h"
#include "VEBundle.h"
#include "VEAudioOutputConfig.h"

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
    #include "libswresample/swresample.h"
    #include "libavutil/opt.h"
}
namespace VE {
    class VEAudioDecoder : public IMediaDecoder{
    public:
        VEAudioDecoder(std::shared_ptr<AMessage> &notify);

        ~VEAudioDecoder() override;

        /// outConfig 由播放器统一决定，解码器按它重采样
        VEResult prepare(std::shared_ptr<IMediaSource> source, const VEAudioOutputConfig &outConfig);

        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult flush() override;

        VEResult release() override;

        VEResult prepare(VEBundle params) override;

        VEResult seekTo(double timestamp) override;

        void needMoreFrame(std::shared_ptr<AMessage> msg) override;

        VEResult readFrame(std::shared_ptr<VEFrame> &frame) override;

    private:
        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart();

        VEResult onPause();

        VEResult onStop();

        VEResult onFlush();

        VEResult onDecode();

        VEResult onRelease();

        VEResult onSeek(double timestampMs);

        VEResult onNeedMoreFrame(const std::shared_ptr<AMessage> &msg);

        /// 投递带当前 epoch 的解码消息，flush 后旧消息会被自动丢弃
        void postDecode();

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
        /// mSeekTargetUs 的哨兵值：当前没有待完成的精准 seek
        static const int64_t kNoSeekTarget = INT64_MIN;

        AVCodecContext *mAudioCtx = nullptr;
        VEMediaInfo *mMediaInfo = nullptr;
        std::shared_ptr<VEFrameQueue> mFrameQueue = nullptr;
        std::shared_ptr<IMediaSource> mDemux = nullptr;

        std::shared_ptr<AMessage> mNotifyEvent = nullptr;

        bool mIsStarted = false;
        bool mNeedMoreData = false;

        /// 解码消息的代次，flush/seek 时递增以作废在途的解码消息
        int32_t mEpoch = 0;
        /// 精准 seek 目标(微秒)，其之前解出的帧全部丢弃
        int64_t mSeekTargetUs = kNoSeekTarget;

        std::shared_ptr<AMessage> mNotifyMore = nullptr;
        bool mIsEOS = false;
        SwrContext *mSwrCtx = nullptr;

        /// 重采样目标，prepare 时由播放器下发
        int32_t mOutSampleRate = 44100;
        int32_t mOutChannels = 2;
        int32_t mOutFormat = AV_SAMPLE_FMT_S16;
    };
}

#endif