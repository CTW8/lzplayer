#ifndef      __VE_DEMUX__
#define      __VE_DEMUX__

#include<string>
#include<memory>
#include<atomic>
#include<mutex>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include "VEPacketQueue.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEError.h"
#include "IMediaSource.h"
#include "VESource.h"

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}
namespace VE {
    /// 本地文件源：LZPlayer 既有的 demux 内核实现 VESource。
    /// 驱动模型(读循环自驱 + 消费侧拉取唤醒 + shouldParkRead 节流 +
    /// interrupt 解阻塞 + mReleased 终态防复活)全部在本类，VESource 只划边界。
    class VEDemux : public VESource {

    public:
        explicit VEDemux(std::shared_ptr<AMessage> &notify);

        ~VEDemux() override;

        // 生命周期命令异步执行(投 kWhatXxx 进自身 looper)，完成后经
        // VESource::postNotify 回执 PREPARE_DONE/STOP_DONE/...。
        // prepareAsync 完成后回 VE_NOTIFY_EVENT_PREPARE_DONE(arg1=结果)
        VEResult prepareAsync(const std::string &path) override;

        /// 中断阻塞中的 FFmpeg IO(open/read)。可从任意线程调用。
        void abort() override;

        VEResult seekTo(double timestamp) override;

        VEResult flush() override;

        VEResult release() override;

        VEResult start() override;

        VEResult stop() override;

        VEResult pause() override;

    public:
        VEResult read(ETrackType type, std::shared_ptr<VEPacket> &packet) override;

        std::shared_ptr<VEMediaInfo> getFileInfo() override;

        void requestReadNotify(ETrackType type,
                               const std::shared_ptr<AMessage> &notify) override;

        // —— 诊断读数：跨线程调用，底层队列自带锁 ——
        /// 指定轨道的待消费包数
        int getQueueDepth(ETrackType type) const;
        /// 两路里较小的缓冲时长(微秒)——短板决定还能撑多久
        int64_t getBufferedDurationUs() const;

        /// 切换活跃轨道(按对外轨道号)。异步执行，完成后回
        /// VE_NOTIFY_EVENT_SELECT_TRACK_DONE。仅音频/字幕轨可切。
        VEResult selectTrack(int trackIndex) override;

    private:
        VEResult onPrepare(std::string path);

        VEResult onStart();

        VEResult onPause();

        VEResult onStop();

        VEResult onFlush();

        VEResult onRelease();

        VEResult onRead();

        VEResult onSeek(double posMs);

        /// 拉取触发补货：读循环该继续跑(未达 park 条件)且无在途续读消息时，
        /// 向自己的 looper 投递 kWhatContinueRead。由消费者线程调用。
        void scheduleContinueReadIfNeeded();

        /// 单路是否已缓冲够(仿 ffplay stream_has_enough_packets)：
        /// 不存在的流视为够了；否则要求包数达标，且(无时长信息 或 时长达标)
        bool streamHasEnough(const std::shared_ptr<VEPacketQueue> &queue, bool exists) const;

        void putPacket(std::shared_ptr<VEPacket> packet, ETrackType type);

        /// 读循环是否该停：唯一硬停是总字节封顶(防 OOM)，否则两路都够了才停。
        /// 注意不按单路 packet 数硬停——那会造成一路满拖死另一路(队头阻塞)。
        bool shouldParkRead() const;

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;
    protected:
        /// 打开输入。基类实现直接 avformat_open_input(本地文件)；
        /// 网络源覆写为"挂自定义 AVIOContext 后再 open"——demux 内核
        /// (读循环/节流/seek/队列/命令面)因此可以原样复用。
        virtual VEResult openInput(AVFormatContext *ctx, const std::string &path);

        /// 缓冲节流参数。网络场景要缓更深，子类可覆写放大。
        virtual size_t maxTotalBytes() const;
        virtual int64_t bufferedDurationTargetUs() const;

        AVFormatContext *mFormatContext = nullptr;
        std::atomic<bool> mAbortRequest{false};

    private:
        /// 按 FFmpeg 流索引扫描全部轨道，填进 mCachedFileInfo->tracks，
        /// 并用 av_find_best_stream 选出默认活跃轨
        VEResult buildTrackList();

        VEResult onSelectTrack(int trackIndex);

        /// 活跃轨道对应的包队列；类型不存在时返回 nullptr
        std::shared_ptr<VEPacketQueue> queueFor(ETrackType type) const;

    private:
        std::string mFilePath;
        uint64_t mDuration = 0;

        /// 当前活跃轨道的 FFmpeg 流索引(-1 表示该链路不存在/已关闭)。
        /// 读循环按它分派包，selectTrack 改的就是这三个值。
        int mAudio_index = -1;
        int mVideo_index = -1;
        int mSubtitle_index = -1;

        // read() 由解码器线程调用，会读取这两个标志，故需原子访问
        std::atomic<bool> mIsStart{false};
        /// 连续读到坏包的计数，读成功即清零；仅 looper 线程访问
        int mReadErrorCount = 0;

        /// 在途续读消息去重(消费者线程与 looper 线程都会碰)
        std::atomic<bool> mContinuePending{false};

        /// 各轨道饥饿方登记的一次性通知。消费者线程写、demux 线程取，
        /// 用锁保护(shared_ptr 的并发读写不是原子的)
        std::mutex mNotifyMutex;
        std::shared_ptr<AMessage> mReadNotify[3];
        /// 终态：release 之后拒绝一切数据面活动，防止超时强推后被复活
        std::atomic<bool> mReleased{false};

        static int interruptCallback(void *opaque);

        // 音视频共用的时间零点偏移(微秒)，保证两条流落在同一时间轴上
        int64_t mStartTimeOffset = 0;

        std::atomic<bool> mIsEOS{false};

        //视频帧
        std::shared_ptr<VEPacketQueue> mVideoPacketQueue = nullptr;
        //音频帧
        std::shared_ptr<VEPacketQueue> mAudioPacketQueue = nullptr;
        /// 字幕包稀疏，单独一条小队列；不参与 shouldParkRead 判据，
        /// 否则字幕流会拖着读循环
        std::shared_ptr<VEPacketQueue> mSubtitlePacketQueue = nullptr;

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
            kWhatSelectTrack = 'sltk',
        };
    };
}
#endif