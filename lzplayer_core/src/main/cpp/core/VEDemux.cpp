#include "VEDemux.h"
#include "platform/android/decoders/VECodecWarmup.h"
#include "VEPacket.h"
#include "VEError.h"

#include <algorithm>

extern "C"{
    #include"libavcodec/avcodec.h"
    #include"libavformat/avformat.h"
    #include"libavutil/dict.h"
    #include"libavutil/display.h"
}

#include <cmath>

namespace VE {
    namespace {
        /// av_read_frame 瞬时无数据(EAGAIN)时的重试间隔
        constexpr int64_t kReadRetryDelayUs = 10000;
        /// 连续坏包的容忍上限，超过按致命错误处理，避免全损文件空转
        constexpr int kMaxConsecutiveReadErrors = 100;

        // ---- 缓冲节流(对齐 ffplay) ----
        /// 所有队列字节总和的硬上限。这是唯一的硬停条件，防高码率 OOM。
        constexpr size_t kMaxTotalBytes = 16 * 1024 * 1024;
        /// 每路"缓冲够了"的目标时长
        constexpr int64_t kBufferedDurationTargetUs = 1000000; // 1s
        /// 每路最低包数(容器无 duration 信息时的唯一门槛)
        constexpr int kMinPackets = 25;
        /// 队列防失控兜底容量：真实节流在 shouldParkRead(字节/时长)，
        /// 这里只是让 put 在极端情况下不至无限增长。远高于正常缓冲深度。
        constexpr int kQueueBackstopPackets = 2048;
        /// 字幕包稀疏(一行一包)，小队列足够；不参与读循环节流判据
        constexpr int kSubtitleQueuePackets = 256;
    }
    VEDemux::VEDemux(std::shared_ptr<AMessage> &notify) : VESource(notify){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        mFormatContext = nullptr;
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }

    VEDemux::~VEDemux() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 析构期间不能用 shared_from_this() 投递消息，直接同步清理
        onRelease();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }

    VEResult VEDemux::prepareAsync(const std::string &path){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 纯异步：完成后回 PREPARE_DONE 事件。原同步版会把 player looper
        // 挂在 avformat_open_input 上，坏文件/网络源可卡死整条命令通道。
        mAbortRequest = false;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setString("filePath", path);
        msg->post();
        return VE_OK;
    }

    void VEDemux::abort() {
        // 可从任意线程调用：让卡在 avformat_open_input/av_read_frame 里的
        // demux 线程尽快返回 AVERROR_EXIT
        mAbortRequest = true;
    }

    // FFmpeg 中断回调：返回非 0 即中断当前阻塞的 IO/解析操作
    int VEDemux::interruptCallback(void *opaque) {
        auto *self = static_cast<VEDemux *>(opaque);
        return (self != nullptr && self->mAbortRequest.load()) ? 1 : 0;
    }


    VEResult VEDemux::start() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::stop() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 先置中断标志再投消息：若线程正卡在 open/read 的阻塞 IO 里，
        // 它会立即以 AVERROR_EXIT 返回，stop 消息才能尽快被处理
        mAbortRequest = true;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::pause() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::seekTo(double posMs) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("posMs", posMs);
        msg->post();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    std::shared_ptr<VEPacketQueue> VEDemux::queueFor(ETrackType type) const {
        switch (type) {
            case ETrackType::AUDIO:    return mAudioPacketQueue;
            case ETrackType::VIDEO:    return mVideoPacketQueue;
            case ETrackType::SUBTITLE: return mSubtitlePacketQueue;
        }
        return nullptr;
    }

    VEResult VEDemux::read(ETrackType type, std::shared_ptr<VEPacket> &packet) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 队列尚未建立(prepare 未完成)：与 scheduleContinueReadIfNeeded 一致的判空
        std::shared_ptr<VEPacketQueue> queue = queueFor(type);
        if (queue == nullptr) {
            return VE_NOT_ENOUGH_DATA;
        }
        if (queue->getDataSize() == 0) {
            ALOGV("VEDemux::read queue empty, wait");
            return VE_NOT_ENOUGH_DATA;
        }
        packet = queue->get();
        // 拉取触发补货(仿 GenericSource::dequeueAccessUnit)：不再用 kWhatStart
        // 命令消息复活 demux——数据面事件不允许触碰命令通道
        scheduleContinueReadIfNeeded();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    int VEDemux::getQueueDepth(ETrackType type) const {
        std::shared_ptr<VEPacketQueue> queue = queueFor(type);
        return queue ? queue->getDataSize() : 0;
    }

    int64_t VEDemux::getBufferedDurationUs() const {
        // 取存在的那几路里最小的：能撑多久由短板决定
        int64_t minUs = -1;
        if (mAudio_index != -1 && mAudioPacketQueue) {
            minUs = mAudioPacketQueue->getDurationUs();
        }
        if (mVideo_index != -1 && mVideoPacketQueue) {
            const int64_t v = mVideoPacketQueue->getDurationUs();
            minUs = (minUs < 0) ? v : std::min(minUs, v);
        }
        return minUs < 0 ? 0 : minUs;
    }

    VEResult VEDemux::selectTrack(int trackIndex) {
        auto msg = std::make_shared<AMessage>(kWhatSelectTrack, shared_from_this());
        msg->setInt32("trackIndex", trackIndex);
        msg->post();
        return VE_OK;
    }

    VEResult VEDemux::onSelectTrack(int trackIndex) {
        if (mCachedFileInfo == nullptr) {
            return VE_INVALID_OPERATION;
        }
        // trackIndex < 0 表示关闭该类型链路(仅字幕有意义)
        if (trackIndex < 0) {
            mSubtitle_index = -1;
            mCachedFileInfo->activeSubtitle = -1;
            if (mSubtitlePacketQueue) mSubtitlePacketQueue->clear();
            ALOGI("VEDemux::%s subtitle track deselected", __FUNCTION__);
            return VE_OK;
        }

        const int slot = mCachedFileInfo->slotOfTrackIndex(trackIndex);
        if (slot < 0) {
            ALOGE("VEDemux::%s unknown track index %d", __FUNCTION__, trackIndex);
            return VE_INVALID_PARAMS;
        }
        const VETrackInfo &track = mCachedFileInfo->tracks[slot];
        switch (track.type) {
            case ETrackType::AUDIO:
                mAudio_index = track.streamIndex;
                mCachedFileInfo->activeAudio = slot;
                if (mAudioPacketQueue) mAudioPacketQueue->clear();
                break;
            case ETrackType::SUBTITLE:
                mSubtitle_index = track.streamIndex;
                mCachedFileInfo->activeSubtitle = slot;
                if (mSubtitlePacketQueue) mSubtitlePacketQueue->clear();
                break;
            case ETrackType::VIDEO:
                // 视频轨切换会牵动整条渲染链与硬解绑定，本期不支持
                ALOGE("VEDemux::%s video track switch not supported", __FUNCTION__);
                return VE_INVALID_OPERATION;
        }
        // 切轨后读位置仍在原处：由播放器紧接着的全链 seek 拉回当前播放位置
        mIsEOS = false;
        ALOGI("VEDemux::%s track %d (stream %d) selected", __FUNCTION__,
              trackIndex, track.streamIndex);
        return VE_OK;
    }

    void VEDemux::requestReadNotify(ETrackType type,
                                    const std::shared_ptr<AMessage> &notify) {
        {
            std::lock_guard<std::mutex> lk(mNotifyMutex);
            mReadNotify[static_cast<int>(type)] = notify;
        }
        // 登记之后要复查一次：登记与"数据刚好入队"之间存在竞态，
        // 不复查的话这次通知会永远等不到(丢唤醒)
        std::shared_ptr<VEPacketQueue> queue = queueFor(type);
        if ((queue && queue->getDataSize() > 0) || mIsEOS) {
            std::shared_ptr<AMessage> pending;
            {
                std::lock_guard<std::mutex> lk(mNotifyMutex);
                pending = mReadNotify[static_cast<int>(type)];
                mReadNotify[static_cast<int>(type)].reset();
            }
            if (pending) {
                pending->post();
            }
        }
    }

    void VEDemux::scheduleContinueReadIfNeeded() {
        if (mReleased || mIsEOS) {
            return;
        }
        // 只要读循环还该继续跑就唤醒它。这里是消费者线程侧的提示(多路
        // 队列状态非原子读无妨)，demux 线程 onRead 会用同一判据权威复查。
        if (!shouldParkRead()) {
            // exchange 去重：已有在途的续读消息就不再投
            if (!mContinuePending.exchange(true)) {
                std::make_shared<AMessage>(kWhatContinueRead, shared_from_this())->post();
            }
        }
    }

    bool VEDemux::streamHasEnough(const std::shared_ptr<VEPacketQueue> &queue, bool exists) const {
        if (!exists || queue == nullptr) {
            return true;   // 不存在的流(纯音频/纯视频)视为"够了"
        }
        if (queue->getDataSize() < kMinPackets) {
            return false;
        }
        const int64_t durUs = queue->getDurationUs();
        // 容器没给 duration(durUs<=0)时只凭包数判断，否则要求时长达标
        return durUs <= 0 || durUs >= bufferedDurationTargetUs();
    }

    bool VEDemux::shouldParkRead() const {
        // 硬停①：所有队列字节总和超上限(防高码率 OOM)
        size_t totalBytes = 0;
        if (mAudioPacketQueue) totalBytes += mAudioPacketQueue->getTotalBytes();
        if (mVideoPacketQueue) totalBytes += mVideoPacketQueue->getTotalBytes();
        if (totalBytes >= maxTotalBytes()) {
            return true;
        }
        // 硬停②：单路包数达队列容量兜底(kQueueBackstopPackets)。
        // 否则单路小包(低码率音频)堆积到 2048 但总字节远未到 16MB 时，
        // put 会满而 putPacket 静默丢包 → 下游花屏/缺音。
        // 让 2048 成为"停读等 credit 回流"门槛，而非"丢包"门槛。
        if (mAudioPacketQueue && mAudioPacketQueue->getDataSize() >= kQueueBackstopPackets) {
            return true;
        }
        if (mVideoPacketQueue && mVideoPacketQueue->getDataSize() >= kQueueBackstopPackets) {
            return true;
        }
        // 软停：两路都缓冲够了才不用继续读。单路满不 park——那正是队头阻塞
        return streamHasEnough(mAudioPacketQueue, mAudio_index != -1)
            && streamHasEnough(mVideoPacketQueue, mVideo_index != -1);
    }

    VEResult VEDemux::release(){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        mAbortRequest = true;   // 同 stop：先解除阻塞 IO
        std::make_shared<AMessage>(kWhatRelease,shared_from_this())->post();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return 0;
    }


    VEResult VEDemux::flush() {
        std::make_shared<AMessage>(kWhatFlush,shared_from_this())->post();
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        return VE_OK;
    }

    std::shared_ptr<VEMediaInfo> VEDemux::getFileInfo() {
        // prepare 完成后 mCachedFileInfo 已就绪且只读，直接返回缓存
        return mCachedFileInfo;
    }

    void VEDemux::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        switch (msg->what()) {
            case kWhatPrepare: {
                std::string path;
                msg->findString("filePath", path);
                VEResult ret = onPrepare(path);
                postNotify(VE_NOTIFY_EVENT_PREPARE_DONE, ret, 0, 0, nullptr);
                break;
            }
            case kWhatStart: {
                mIsStart = true;
                mIsEOS = false;
                mAbortRequest = false;   // stop 后重新 start：解除中断标志
                onStart();
                break;
            }
            case kWhatStop: {
                mIsStart = false;
                onStop();
                postNotify(VE_NOTIFY_EVENT_STOP_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatPause: {
                // 处理到这条消息即代表读取循环已停止：kWhatRead 会检查 mIsStart，
                // 排在它之前的读取消息此时都已执行完毕。
                mIsStart = false;
                onPause();
                postNotify(VE_NOTIFY_EVENT_PAUSE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatSeek: {
                double pos = 0;
                msg->findDouble("posMs", &pos);
                VEResult ret = onSeek(pos);
                postNotify(VE_NOTIFY_EVENT_SEEK_DONE, ret, 0, 0, nullptr);
                break;
            }
            case kWhatRead: {
                if (!mIsStart) {
                    ALOGD("VEDemux::%s kWhatRead !mIsStart not run!!!", __FUNCTION__);
                    break;
                }
                ALOGV("VEDemux::%s kWhatRead run", __FUNCTION__);
                VEResult ret = onRead();
                if (ret == VE_OK) {
                    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,
                                                                               shared_from_this());
                    msg->post();
                } else if (ret == VE_ERROR_EAGAIN) {
                    // 瞬时错误：延时续投，保持读循环存活
                    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,
                                                                               shared_from_this());
                    msg->post(kReadRetryDelayUs);
                }
                // VE_EOS/致命错误：停止续投(致命路径已复位 mIsStart 并上报)
                break;
            }
            case kWhatContinueRead: {
                mContinuePending = false;
                // 数据面唤醒只重新拉起读循环，不触碰命令态：
                // 停止(stop)/EOS/已释放 时即便被叫醒也不动
                if (mIsStart && !mIsEOS && !mReleased) {
                    std::make_shared<AMessage>(kWhatRead, shared_from_this())->post();
                }
                break;
            }
            case kWhatSelectTrack: {
                int32_t trackIndex = -1;
                msg->findInt32("trackIndex", &trackIndex);
                VEResult ret = onSelectTrack(trackIndex);
                postNotify(VE_NOTIFY_EVENT_SELECT_TRACK_DONE, ret, 0, 0, nullptr);
                break;
            }
            case kWhatRelease:{
                onRelease();
                // 资源已在本线程释放完毕，上层收齐回执后才会停这条 looper
                postNotify(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
                break;
            }
            default:{
                ALOGW("VEDemux::%s unknown message: %d", __FUNCTION__, msg->what());
                break;
            }
        }
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }

    VEResult VEDemux::onPrepare(std::string path){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        ///打开文件
        if (path.empty()) {
            ALOGE("VEDemux::%s open file failed: empty path", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mFilePath = path;

        // 挂中断回调：stop/release/abort 置位后，卡在 open/read 的线程
        // 能立即以 AVERROR_EXIT 返回，teardown 不再受阻塞 IO 摆布
        mFormatContext = avformat_alloc_context();
        if (mFormatContext == nullptr) {
            return VE_NO_MEMORY;
        }
        mFormatContext->interrupt_callback.callback = &VEDemux::interruptCallback;
        mFormatContext->interrupt_callback.opaque = this;

        // 本地走文件路径，网络源覆写此处挂自定义 AVIOContext
        if (openInput(mFormatContext, mFilePath) != VE_OK) {
            ALOGE("VEDemux::onPrepare couldn't open input");
            return VE_UNKNOWN_ERROR;
        }
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T1_CONTAINER_OPEN);
        }

        // 硬解 codec 实例创建要 50~66ms，且只依赖 mime。此刻 open_input 已
        // 返回、codec_id 已知，而 find_stream_info 还要跑 51~66ms——把创建
        // 塞进那段窗口里并行做，正好互相抵消。不是猜 mime，是拿准了才预热。
        for (unsigned int i = 0; i < mFormatContext->nb_streams; ++i) {
            AVStream *st = mFormatContext->streams[i];
            if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
                continue;
            }
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                continue;   // 内嵌封面不是可播放视频流
            }
            VECodecWarmup::warmUpForCodec(st->codecpar->codec_id);
            break;
        }

        // 收紧探测上限：启播链路最大的一笔可省开销(本地 1080p 实测
        // 133~145ms)。收得过狠会漏轨道，所以具体值交给 probeLimits()，
        // 网络源覆写成宽松值
        const ProbeLimits limits = probeLimits();
        if (limits.probeSizeBytes > 0) {
            mFormatContext->probesize = limits.probeSizeBytes;
        }
        if (limits.analyzeDurationUs > 0) {
            mFormatContext->max_analyze_duration = limits.analyzeDurationUs;
        }
        if (limits.fpsProbeFrames >= 0) {
            // fpsprobesize 只有 AVOption，没有结构体字段
            av_opt_set_int(mFormatContext, "fpsprobesize", limits.fpsProbeFrames, 0);
        }
        ALOGI("VEDemux::onPrepare probe: size=%lld analyze=%lldus fpsFrames=%lld",
              (long long) limits.probeSizeBytes, (long long) limits.analyzeDurationUs,
              (long long) limits.fpsProbeFrames);

        // 获取流信息
        if (avformat_find_stream_info(mFormatContext, nullptr) < 0) {
            ALOGE("VEDemux::onPrepare couldn't find stream information");
            avformat_close_input(&mFormatContext);
            return VE_UNKNOWN_ERROR;
        }
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T2_STREAM_INFO);
        }
        // duration 可能为 AV_NOPTS_VALUE(部分流式容器)，不能直接除以 1000
        mDuration = (mFormatContext->duration != AV_NOPTS_VALUE)
                    ? mFormatContext->duration / 1000 : 0;

        VEResult ret = buildTrackList();
        if (ret != VE_OK) {
            return ret;
        }
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T3_TRACKS_READY);
        }

        mAudioPacketQueue = std::make_shared<VEPacketQueue>(kQueueBackstopPackets);
        mVideoPacketQueue = std::make_shared<VEPacketQueue>(kQueueBackstopPackets);
        mSubtitlePacketQueue = std::make_shared<VEPacketQueue>(kSubtitleQueuePackets);

        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::openInput(AVFormatContext *ctx, const std::string &path) {
        // avformat_open_input 会在失败时释放并置空传入的 ctx，
        // 所以这里必须传成员的地址而不是局部副本
        (void) ctx;
        if (avformat_open_input(&mFormatContext, path.c_str(), nullptr, nullptr) != 0) {
            return VE_UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    size_t VEDemux::maxTotalBytes() const { return kMaxTotalBytes; }

    int64_t VEDemux::bufferedDurationTargetUs() const { return kBufferedDurationTargetUs; }

    VEResult VEDemux::buildTrackList() {
        // 媒体信息此后只在 selectTrack 时改活跃轨，其余字段不变；
        // codecParams 由 VEMediaInfo 深拷贝自持，源释放后解码器仍可安全使用
        mCachedFileInfo = std::make_shared<VEMediaInfo>();
        mCachedFileInfo->duration = mDuration;

        // 默认轨用 av_find_best_stream 选(会参考 disposition/DEFAULT 与解码器可用性)，
        // 而不是"扫到的最后一条"——多音轨文件下后者会选错
        const int bestAudio =
                av_find_best_stream(mFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        const int bestVideo =
                av_find_best_stream(mFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int bestSubtitle =
                av_find_best_stream(mFormatContext, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);

        for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
            AVStream *stream = mFormatContext->streams[i];
            const AVMediaType mediaType = stream->codecpar->codec_type;

            ETrackType type;
            if (mediaType == AVMEDIA_TYPE_AUDIO) {
                type = ETrackType::AUDIO;
            } else if (mediaType == AVMEDIA_TYPE_VIDEO) {
                // 跳过 attached_pic(如 MP3 内嵌封面)：它是单帧封面而非可播放
                // 视频流，误当视频会建整套视频链路，且 seek 后等首帧只能吃满超时
                if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                    ALOGI("VEDemux::%s skip attached_pic stream %u", __FUNCTION__, i);
                    continue;
                }
                type = ETrackType::VIDEO;
            } else if (mediaType == AVMEDIA_TYPE_SUBTITLE) {
                // 位图字幕(PGS/DVB)本期不支持，不上报给上层
                const AVCodecID sid = stream->codecpar->codec_id;
                if (sid != AV_CODEC_ID_SUBRIP && sid != AV_CODEC_ID_TEXT &&
                    sid != AV_CODEC_ID_ASS && sid != AV_CODEC_ID_SSA &&
                    sid != AV_CODEC_ID_MOV_TEXT && sid != AV_CODEC_ID_WEBVTT) {
                    ALOGI("VEDemux::%s skip non-text subtitle stream %u (codec %d)",
                          __FUNCTION__, i, sid);
                    continue;
                }
                type = ETrackType::SUBTITLE;
            } else {
                continue;   // 数据流/附件等
            }

            VETrackInfo track;
            track.index = static_cast<int>(mCachedFileInfo->tracks.size());
            track.streamIndex = static_cast<int>(i);
            track.type = type;
            track.codecId = stream->codecpar->codec_id;
            track.timeBase = stream->time_base;
            track.startTime = stream->start_time;
            track.codecParams = avcodec_parameters_alloc();
            if (track.codecParams == nullptr) {
                return VE_NO_MEMORY;
            }
            avcodec_parameters_copy(track.codecParams, stream->codecpar);

            AVDictionaryEntry *e = av_dict_get(stream->metadata, "language", nullptr, 0);
            if (e && e->value) track.lang = e->value;
            e = av_dict_get(stream->metadata, "title", nullptr, 0);
            if (e && e->value) track.title = e->value;

            if (type == ETrackType::VIDEO) {
                track.width = track.codecParams->width;
                track.height = track.codecParams->height;
                // r_frame_rate.den 个别流为 0，除法前判零避免 SIGFPE
                track.fps = (stream->r_frame_rate.den != 0)
                            ? stream->r_frame_rate.num / stream->r_frame_rate.den : 0;
                // 竖拍视频靠容器的 display matrix 标注旋转，渲染器要据此摆正画面
                const uint8_t *dm = av_stream_get_side_data(
                        stream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
                if (dm != nullptr) {
                    const double theta =
                            -av_display_rotation_get(reinterpret_cast<const int32_t *>(dm));
                    int deg = static_cast<int>(lround(theta)) % 360;
                    if (deg < 0) deg += 360;
                    // 只支持 90 度的整数倍，其余按不旋转处理
                    track.rotationDegrees = (deg % 90 == 0) ? deg : 0;
                    ALOGI("VEDemux::%s stream %u rotation %d", __FUNCTION__, i,
                          track.rotationDegrees);
                }
            } else if (type == ETrackType::AUDIO) {
                track.sampleRate = track.codecParams->sample_rate;
                track.channels = track.codecParams->ch_layout.nb_channels;
                track.sampleFormat = track.codecParams->format;
            }

            const int slot = track.index;
            if (type == ETrackType::AUDIO && static_cast<int>(i) == bestAudio) {
                mCachedFileInfo->activeAudio = slot;
                mAudio_index = static_cast<int>(i);
            } else if (type == ETrackType::VIDEO && static_cast<int>(i) == bestVideo) {
                mCachedFileInfo->activeVideo = slot;
                mVideo_index = static_cast<int>(i);
            } else if (type == ETrackType::SUBTITLE && static_cast<int>(i) == bestSubtitle) {
                // 字幕默认不开：记住候选轨但不置活跃，等上层 selectTrack
                ALOGI("VEDemux::%s default subtitle candidate: track %d", __FUNCTION__, slot);
            }

            mCachedFileInfo->tracks.push_back(track);
        }

        // 音视频必须使用同一个时间零点，否则 AVSync 比较的是两条不同基准的时间轴。
        // 取各流 start_time 的较小值作为全局偏移：既把播放起点归一到 0，
        // 又保留了两条流之间真实存在的相对偏移。
        mStartTimeOffset = 0;
        int64_t offset = INT64_MAX;
        const VETrackInfo *v = mCachedFileInfo->videoTrack();
        const VETrackInfo *a = mCachedFileInfo->audioTrack();
        if (v && v->startTime != AV_NOPTS_VALUE) {
            offset = std::min(offset, av_rescale_q(v->startTime, v->timeBase, AV_TIME_BASE_Q));
        }
        if (a && a->startTime != AV_NOPTS_VALUE) {
            offset = std::min(offset, av_rescale_q(a->startTime, a->timeBase, AV_TIME_BASE_Q));
        }
        if (offset != INT64_MAX && offset > 0) {
            mStartTimeOffset = offset;
        }
        ALOGI("VEDemux::%s %zu tracks, activeA=%d activeV=%d, start offset:%" PRId64,
              __FUNCTION__, mCachedFileInfo->tracks.size(),
              mCachedFileInfo->activeAudio, mCachedFileInfo->activeVideo, mStartTimeOffset);
        return VE_OK;
    }

    VEResult VEDemux::onStart() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        mIsEOS = false;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead, shared_from_this());
        msg->post();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEDemux::onRead() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);

        if (mReleased || mFormatContext == nullptr) {
            // 终态防护：teardown 超时强推后可能有迟到的读消息
            ALOGW("VEDemux::onRead after release, ignore");
            return VE_UNKNOWN_ERROR;
        }

        // 缓冲够了 → 停止续投(隐式 park)。这里不动 mIsStart：它是命令态，
        // 由 start/stop 管理；读循环的恢复由消费端拉取触发(kWhatContinueRead)。
        // 判据是"两路都够 或 总字节超限"，不按单路满停——避免一路满拖死另一路。
        if (shouldParkRead()) {
            ALOGD("VEDemux::onRead buffered enough, parking read loop.");
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_NO_MEMORY;
        }

        std::shared_ptr<VEPacket> packet = std::make_shared<VEPacket>();
        if (!packet) {
            ALOGD("VEDemux::onRead Could not allocate AVPacket");
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return NO_ERROR;
        }

        int ret = av_read_frame(mFormatContext, packet->getPacket());
        if (ret == AVERROR_EOF) {
            // 已经到达文件末尾
            ALOGI("VEDemux::onRead End of Stream (EOS) reached.");
            packet->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(packet, ETrackType::AUDIO);

            std::shared_ptr<VEPacket> videoPacket = std::make_shared<VEPacket>();
            videoPacket->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(videoPacket, ETrackType::VIDEO);
            mIsEOS = true;
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_EOS;
        } else if (ret == AVERROR(EAGAIN)) {
            // 瞬时无数据(部分 demuxer/IO 会出现)：延时重试，读循环不能就此死掉
            ALOGW("VEDemux::onRead EAGAIN, retry later");
            return VE_ERROR_EAGAIN;
        } else if (ret == AVERROR_INVALIDDATA &&
                   ++mReadErrorCount < kMaxConsecutiveReadErrors) {
            // 局部损坏的包：跳过继续读，连续超限才视为致命
            ALOGW("VEDemux::onRead skip corrupt packet (%d in a row)", mReadErrorCount);
            return VE_OK;
        } else if (ret < 0) {
            // 致命错误：复位读状态并上报。原实现 mIsStart 卡在 true，
            // 所有重启入口(needMorePacket/read)都被挡住，播放无声无画卡死
            ALOGE("VEDemux::onRead fatal error: %s", av_err2str(ret));
            mIsStart = false;
            postNotify(VE_NOTIFY_EVENT_ERROR, ret, 0, 0, nullptr);
            return VE_UNKNOWN_ERROR;
        }
        mReadErrorCount = 0;

        const AVRational streamTimeBase =
                mFormatContext->streams[packet->getPacket()->stream_index]->time_base;

        // AV_NOPTS_VALUE 不能参与 rescale，否则会得到一个巨大的伪时间戳
        auto toMicros = [&](int64_t ts) -> int64_t {
            if (ts == AV_NOPTS_VALUE) {
                return AV_NOPTS_VALUE;
            }
            return av_rescale_q(ts, streamTimeBase, AV_TIME_BASE_Q) - mStartTimeOffset;
        };

        int64_t pts = toMicros(packet->getPacket()->pts);
        int64_t dts = toMicros(packet->getPacket()->dts);

        // 包时长(微秒)供队列缓冲时长记账用；容器未提供时存 0，
        // 节流会退化为只按包数(见 streamHasEnough)
        if (packet->getPacket()->duration > 0) {
            packet->setDurationUs(av_rescale_q(packet->getPacket()->duration,
                                               streamTimeBase, AV_TIME_BASE_Q));
        }

        const int streamIndex = packet->getPacket()->stream_index;
        if (streamIndex == mAudio_index) {
            packet->setPacketType(E_PACKET_TYPE_AUDIO);
            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = pts;
            packet->getPacket()->dts = dts;
            ALOGV("VEDemux::onRead Audio packet pts:%" PRId64 " dts:%" PRId64, pts, dts);
            putPacket(packet, ETrackType::AUDIO);
        } else if (streamIndex == mVideo_index) {
            packet->setPacketType(E_PACKET_TYPE_VIDEO);
            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = pts;
            packet->getPacket()->dts = dts;
            ALOGV("VEDemux::onRead Video packet pts:%" PRId64 " dts:%" PRId64, pts, dts);
            putPacket(packet, ETrackType::VIDEO);
        } else if (streamIndex == mSubtitle_index) {
            packet->setPacketType(E_PACKET_TYPE_SUBTITLE);
            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = pts;
            packet->getPacket()->dts = dts;
            putPacket(packet, ETrackType::SUBTITLE);
        } else {
            // 非活跃轨道的包(未选中的音轨/字幕轨等)：直接丢弃
            ALOGV("VEDemux::onRead drop packet from inactive stream %d", streamIndex);
        }
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEDemux::onSeek(double posMs) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        if (!mFormatContext) {
            ALOGE("VEDemux::onSeek Error: File not opened.\n");
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }

        ALOGD("VEDemux::onSeek posMs:%f", posMs);

        // 目标位置是相对于归一化后的时间轴，回退到容器原始时间轴需加回偏移
        int64_t targetPts = static_cast<int64_t>(posMs * 1000) + mStartTimeOffset;

        // 优先按视频流定位(关键帧对齐)；纯音频文件退化为音频流
        int seekStreamIndex = (mVideo_index != -1) ? mVideo_index : mAudio_index;
        if (seekStreamIndex == -1) {
            ALOGE("VEDemux::onSeek no seekable stream");
            return VE_INVALID_PARAMS;
        }

        int64_t seekTarget = av_rescale_q(targetPts, AV_TIME_BASE_Q,
                                          mFormatContext->streams[seekStreamIndex]->time_base);

        int ret = avformat_seek_file(mFormatContext, seekStreamIndex, INT64_MIN, seekTarget,
                                     INT64_MAX, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            ALOGE("VEDemux::onSeek Error: Couldn't seek using avformat_seek_file.\n");
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mAudioPacketQueue->clear();
        mVideoPacketQueue->clear();
        if (mSubtitlePacketQueue) mSubtitlePacketQueue->clear();
        // seek 后重新回到"有数据可读"状态，否则 EOS 标志会让读取循环不再启动
        mIsEOS = false;

        ALOGD("VEDemux::onSeek Successful to posMs: %f", posMs);
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEDemux::putPacket(std::shared_ptr<VEPacket> packet, ETrackType type) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 只在 demux 自己的 looper 线程上执行，无需加锁。
        // 消费者饥饿时按 10ms 轮询重试(NuPlayer DecoderBase 的做法)，
        // 不再需要"有数据就通知"的登记机制。
        std::shared_ptr<VEPacketQueue> queue = queueFor(type);
        if (queue == nullptr) {
            return;
        }
        if (!queue->put(packet)) {
            // onRead 入口已做满检查，单生产者下不应走到这里
            // (字幕队列不参与节流，满了直接丢最旧的语义由上层不敏感兜住)
            ALOGW("VEDemux::putPacket queue full (type %d), packet dropped",
                  static_cast<int>(type));
        }
        if (mPerfStats) {
            // 峰值只在生产侧更新即可：队列只在这里变长，消费侧只让它变短
            const int depth = queue->getDataSize();
            if (type == ETrackType::AUDIO) {
                VEPerfStats::bumpPeak(mPerfStats->audioQueuePeak, depth);
            } else if (type == ETrackType::VIDEO) {
                VEPerfStats::bumpPeak(mPerfStats->videoQueuePeak, depth);
            }
        }
        // 唤醒等这条轨道数据的消费者(one-shot：取走即清)
        std::shared_ptr<AMessage> notify;
        {
            std::lock_guard<std::mutex> lk(mNotifyMutex);
            notify.swap(mReadNotify[static_cast<int>(type)]);
        }
        if (notify) {
            notify->post();
        }
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }


    VEResult VEDemux::onPause() {
        // 读取循环已由 mIsStart 停下，这里不需要额外动作
        return VE_OK;
    }

    VEResult VEDemux::onStop() {
        // 停止读取并丢掉已缓存的包，避免下次 start 时吐出上一轮的残留数据
        mIsStart = false;
        mIsEOS = false;
        if (mAudioPacketQueue) mAudioPacketQueue->clear();
        if (mVideoPacketQueue) mVideoPacketQueue->clear();
        if (mSubtitlePacketQueue) mSubtitlePacketQueue->clear();
        return VE_OK;
    }

    VEResult VEDemux::onFlush() {
        mIsEOS = false;
        if (mAudioPacketQueue) mAudioPacketQueue->clear();
        if (mVideoPacketQueue) mVideoPacketQueue->clear();
        if (mSubtitlePacketQueue) mSubtitlePacketQueue->clear();
        return VE_OK;
    }

    VEResult VEDemux::onRelease() {
        mIsStart = false;
        // 终态：此后拒绝一切数据面活动(read/续读)，防止被迟到消息复活
        mReleased = true;
        if (mAudioPacketQueue) mAudioPacketQueue->clear();
        if (mVideoPacketQueue) mVideoPacketQueue->clear();
        if (mSubtitlePacketQueue) mSubtitlePacketQueue->clear();

        if (mFormatContext) {
            avformat_close_input(&mFormatContext);
            mFormatContext = nullptr;
        }

        // codec params 现在由 VEMediaInfo 深拷贝自持并在其析构时释放，
        // 这里不能再动——解码器可能还持有那份 VEMediaInfo
        return VE_OK;
    }
}