#ifndef      __VE_DEMUX__
#define      __VE_DEMUX__

#include<string>
#include<memory>
#include<atomic>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include "VEPacketQueue.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEError.h"
#include "IMediaSource.h"

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}
namespace VE {
    class VEDemux : public AHandler, public IMediaSource {

    public:
        explicit VEDemux(std::shared_ptr<AMessage> &notify);

        ~VEDemux() override;

        // 生命周期命令由 VEPlayer 持具体类型直接调用，不再经接口。
        // prepare 为异步：完成后回 VE_NOTIFY_EVENT_PREPARE_DONE(arg1=结果)
        VEResult prepare(const std::string &path);

        /// 中断阻塞中的 FFmpeg IO(open/read)。可从任意线程调用。
        void abort();

        VEResult seekTo(double timestamp);

        VEResult flush();

        VEResult release();

        VEResult start();

        VEResult stop();

        VEResult pause();

    public:
        VEResult read(bool isAudio, std::shared_ptr<VEPacket> &packet) override;

        std::shared_ptr<VEMediaInfo> getFileInfo() override;

    private:
        VEResult onPrepare(std::string path);

        VEResult onStart();

        VEResult onPause();

        VEResult onStop();

        VEResult onFlush();

        VEResult onRelease();

        VEResult onRead();

        VEResult onSeek(double posMs);

        void putPacket(std::shared_ptr<VEPacket> packet, bool isAudio);

        /// 拉取触发补货：读循环该继续跑(未达 park 条件)且无在途续读消息时，
        /// 向自己的 looper 投递 kWhatContinueRead。由消费者线程调用。
        void scheduleContinueReadIfNeeded();

        /// 单路是否已缓冲够(仿 ffplay stream_has_enough_packets)：
        /// 不存在的流视为够了；否则要求包数达标，且(无时长信息 或 时长达标)
        bool streamHasEnough(const std::shared_ptr<VEPacketQueue> &queue, bool exists) const;

        /// 读循环是否该停：唯一硬停是总字节封顶(防 OOM)，否则两路都够了才停。
        /// 注意不按单路 packet 数硬停——那会造成一路满拖死另一路(队头阻塞)。
        bool shouldParkRead() const;

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);
    private:
        std::string mFilePath;
        int32_t mWidth = 0;
        int32_t mHeight = 0;
        uint64_t mDuration = 0;
        int32_t mFps = 0;
        AVCodecParameters *mVideoCodecParams = nullptr;
        AVRational mVideoTimeBase;
        int64_t mVStartTime = 0;

        std::shared_ptr<AMessage> mNotifyEvent = nullptr;

        int32_t mSampleRate = 0;
        int32_t mChannel = 0;
        int32_t mSampleFormat = 0;
        AVCodecParameters *mAudioCodecParams = nullptr;
        AVRational mAudioTimeBase;
        int64_t mAStartTime = 0;

        int mAudio_index = -1;
        int mVideo_index = -1;

        // read() 由解码器线程调用，会读取这两个标志，故需原子访问
        std::atomic<bool> mIsStart{false};
        /// 连续读到坏包的计数，读成功即清零；仅 looper 线程访问
        int mReadErrorCount = 0;

        /// 在途续读消息去重(消费者线程与 looper 线程都会碰)
        std::atomic<bool> mContinuePending{false};
        /// 终态：release 之后拒绝一切数据面活动，防止超时强推后被复活
        std::atomic<bool> mReleased{false};
        /// FFmpeg 中断标志：stop/release/abort 置位，prepare 入口清零
        std::atomic<bool> mAbortRequest{false};

        static int interruptCallback(void *opaque);

        // 音视频共用的时间零点偏移(微秒)，保证两条流落在同一时间轴上
        int64_t mStartTimeOffset = 0;

        std::atomic<bool> mIsEOS{false};

        //视频帧
        std::shared_ptr<VEPacketQueue> mVideoPacketQueue = nullptr;
        //音频帧
        std::shared_ptr<VEPacketQueue> mAudioPacketQueue = nullptr;
        AVFormatContext *mFormatContext = nullptr;

        /// prepare 完成后媒体信息即不变，缓存一份直接返回，
        /// 免去每次 getFileInfo 都 new + 逐字段拷贝
        std::shared_ptr<VEMediaInfo> mCachedFileInfo = nullptr;

    private:
        enum {
            kWhatPrepare = 'prep',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatSeek = 'seek',
            kWhatFlush = 'flus',
            kWhatRead = 'read',
            kWhatRelease = 'rele',
            kWhatContinueRead = 'cont',
        };
    };
}
#endif