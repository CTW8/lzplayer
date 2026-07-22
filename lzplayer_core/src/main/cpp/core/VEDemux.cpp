#include "VEDemux.h"
#include "VEPacket.h"
#include "VEError.h"

#include <algorithm>

extern "C"{
    #include"libavcodec/avcodec.h"
    #include"libavformat/avformat.h"
    #include"libavutil/dict.h"
}

#define AUDIO_QUEUE_SIZE    100
#define VIDEO_QUEUE_SIZE    100
namespace VE {
    VEDemux::VEDemux(std::shared_ptr<AMessage> &notify) :mNotifyEvent(notify){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        mAudioCodecParams = nullptr;
        mVideoCodecParams = nullptr;
        mFormatContext = nullptr;
        mNotifyEvent = notify;
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }

    VEDemux::~VEDemux() {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 析构期间不能用 shared_from_this() 投递消息，直接同步清理
        onRelease();
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }

    VEResult VEDemux::prepare(VE::VEBundle params){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 必须同步：调用方紧接着就会 getFileInfo()，异步 post 会读到尚未填充的媒体信息
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setString("filePath", params.get<std::string >("path"));

        std::shared_ptr<AMessage> response;
        if (msg->postAndAwaitResponse(&response) != OK || response == nullptr) {
            ALOGE("VEDemux::%s post prepare failed", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        int32_t ret = VE_UNKNOWN_ERROR;
        response->findInt32("ret", &ret);
        ALOGV("VEDemux::%s exit ret:%d", __FUNCTION__, ret);
        return ret;
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

    VEResult VEDemux::read(bool isAudio, std::shared_ptr<VEPacket> &packet) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        ALOGV("VEDemux::read audio queue size: %d, video queue size: %d",
              mAudioPacketQueue->getDataSize(), mVideoPacketQueue->getDataSize());
        if (isAudio) {
            ALOGV("VEDemux::read mAudioPacketQueue size:%d", mAudioPacketQueue->getDataSize());
            if (mAudioPacketQueue->getDataSize() == 0) {
                ALOGV("VEDemux::read audio queue wait!!");
                ALOGV("VEDemux::%s exit", __FUNCTION__);
                return VE_NOT_ENOUGH_DATA;
            }

            packet = mAudioPacketQueue->get();
        } else {
            ALOGV("VEDemux::read mVideoPacketQueue size:%d", mVideoPacketQueue->getDataSize());
            if (mVideoPacketQueue->getDataSize() == 0) {
                ALOGV("VEDemux::read video queue wait!!");
                ALOGV("VEDemux::%s exit", __FUNCTION__);
                return VE_NOT_ENOUGH_DATA;
            }

            packet = mVideoPacketQueue->get();
        }
        if(mVideoPacketQueue->getRemainingSize()>0 && mAudioPacketQueue->getRemainingSize()>0 && !mIsEOS && !mIsStart){
            std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
            msg->post();
        }
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::release(){
        ALOGV("VEDemux::%s enter", __FUNCTION__);
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
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<VEMediaInfo> tmp = std::make_shared<VEMediaInfo>();

        tmp->channels = mChannel;
        tmp->duration = mDuration;
        tmp->fps = mFps;
        tmp->width = mWidth;
        tmp->height = mHeight;
        tmp->sampleRate = mSampleRate;
        tmp->sampleFormat = mSampleFormat;
        tmp->mAudioCodecParams = mAudioCodecParams;
        tmp->mVideoCodecParams = mVideoCodecParams;
        tmp->audio_stream_index = mAudio_index;
        tmp->video_stream_index = mVideo_index;
        tmp->mAStartTime = mAStartTime;
        tmp->mAudioTimeBase = mAudioTimeBase;
        tmp->mVideoTimeBase = mVideoTimeBase;
        tmp->mVStartTime = mVStartTime;
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return tmp;
    }

    void VEDemux::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        switch (msg->what()) {
            case kWhatPrepare: {
                std::string path;
                msg->findString("filePath", path);
                VEResult ret = onPrepare(path);

                std::shared_ptr<AReplyToken> replyID;
                if (msg->senderAwaitsResponse(replyID)) {
                    std::shared_ptr<AMessage> response = std::make_shared<AMessage>();
                    response->setInt32("ret", ret);
                    response->postReply(replyID);
                }
                break;
            }
            case kWhatStart: {
                mIsStart = true;
                mIsEOS = false;
                onStart();
                break;
            }
            case kWhatStop: {
                mIsStart = false;
                onStop();
                postMessage(VE_NOTIFY_EVENT_STOP_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatPause: {
                // 处理到这条消息即代表读取循环已停止：kWhatRead 会检查 mIsStart，
                // 排在它之前的读取消息此时都已执行完毕。
                mIsStart = false;
                onPause();
                postMessage(VE_NOTIFY_EVENT_PAUSE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatSeek: {
                double pos = 0;
                msg->findDouble("posMs", &pos);
                VEResult ret = onSeek(pos);
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE, ret, 0, 0, nullptr);
                break;
            }
            case kWhatRead: {
                if (!mIsStart) {
                    ALOGD("VEDemux::%s kWhatRead !mIsStart not run!!!", __FUNCTION__);
                    break;
                }
                ALOGD("VEDemux::%s kWhatRead run", __FUNCTION__);
                if (onRead() == VE_OK) {
                    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,
                                                                               shared_from_this());
                    msg->post();
                }
                break;
            }
            case kWhatNeedMore: {
                onNeedMorePacket(msg);
                break;
            }
            case kWhatRelease:{
                onRelease();
                // 资源已在本线程释放完毕，上层收齐回执后才会停这条 looper
                postMessage(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
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
            printf("## %s  %d open file failed!!!", __FUNCTION__, __LINE__);
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mFilePath = path;

        if (avformat_open_input(&mFormatContext, mFilePath.c_str(), nullptr, nullptr) != 0) {
            fprintf(stderr, "Error: Couldn't open input file.\n");
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        // 获取流信息
        if (avformat_find_stream_info(mFormatContext, nullptr) < 0) {
            fprintf(stderr, "Error: Couldn't find stream information.\n");
            avformat_close_input(&mFormatContext);
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }
        mDuration = mFormatContext->duration / 1000;
        ///获取文件信息
        for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
            AVStream *stream = mFormatContext->streams[i];

            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                mAudio_index = i;
                mAudioTimeBase = stream->time_base;
                mAStartTime = stream->start_time;
                mAudioCodecParams = avcodec_parameters_alloc();
                avcodec_parameters_copy(mAudioCodecParams, stream->codecpar);
                mChannel = mAudioCodecParams->ch_layout.nb_channels;
                mSampleFormat = mAudioCodecParams->format;
                mSampleRate = mAudioCodecParams->sample_rate;
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                mVideo_index = i;
                mVideoTimeBase = stream->time_base;
                mVStartTime = stream->start_time;
                mVideoCodecParams = avcodec_parameters_alloc();
                avcodec_parameters_copy(mVideoCodecParams, stream->codecpar);
                mWidth = mVideoCodecParams->width;
                mHeight = mVideoCodecParams->height;
                mFps = stream->r_frame_rate.num / stream->r_frame_rate.den;
            }
        }

        // 音视频必须使用同一个时间零点，否则 AVSync 比较的是两条不同基准的时间轴。
        // 取各流 start_time 的较小值作为全局偏移：既把播放起点归一到 0，
        // 又保留了两条流之间真实存在的相对偏移。
        mStartTimeOffset = 0;
        int64_t offset = INT64_MAX;
        if (mVideo_index != -1 && mVStartTime != AV_NOPTS_VALUE) {
            offset = std::min(offset, av_rescale_q(mVStartTime, mVideoTimeBase, AV_TIME_BASE_Q));
        }
        if (mAudio_index != -1 && mAStartTime != AV_NOPTS_VALUE) {
            offset = std::min(offset, av_rescale_q(mAStartTime, mAudioTimeBase, AV_TIME_BASE_Q));
        }
        if (offset != INT64_MAX && offset > 0) {
            mStartTimeOffset = offset;
        }
        ALOGI("VEDemux::%s start time offset:%" PRId64, __FUNCTION__, mStartTimeOffset);

        mAudioPacketQueue = std::make_shared<VEPacketQueue>(AUDIO_QUEUE_SIZE);
        mVideoPacketQueue = std::make_shared<VEPacketQueue>(VIDEO_QUEUE_SIZE);
        ALOGV("VEDemux::%s exit", __FUNCTION__);
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

        if (mAudioPacketQueue->getDataSize() >= AUDIO_QUEUE_SIZE) {
            ALOGD("VEDemux::onRead Audio queue is full, stopping read.");
            mIsStart = false;
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_NO_MEMORY;
        }

        if (mVideoPacketQueue->getDataSize() >= VIDEO_QUEUE_SIZE) {
            ALOGD("VEDemux::onRead Video queue is full, stopping read.");
            mIsStart = false;
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
            putPacket(packet, true);

            std::shared_ptr<VEPacket> videoPacket = std::make_shared<VEPacket>();
            videoPacket->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(videoPacket, false);
            mIsEOS = true;
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_EOS;
        } else if (ret < 0) {
            // 处理其他错误
            ALOGI("VEDemux::onRead Error occurred: %s", av_err2str(ret));
            ALOGV("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

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

        if (packet->getPacket()->stream_index == mAudio_index) {
            packet->setPacketType(E_PACKET_TYPE_AUDIO);
            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = pts;
            packet->getPacket()->dts = dts;
            ALOGV("VEDemux::onRead Audio packet pts:%" PRId64 " dts:%" PRId64, pts, dts);
            putPacket(packet, true);
        } else if (packet->getPacket()->stream_index == mVideo_index) {
            packet->setPacketType(E_PACKET_TYPE_VIDEO);
            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = pts;
            packet->getPacket()->dts = dts;
            ALOGV("VEDemux::onRead Video packet pts:%" PRId64 " dts:%" PRId64, pts, dts);
            putPacket(packet, false);
        } else {
            ALOGD("VEDemux::onRead may be not use");
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
        // seek 后重新回到"有数据可读"状态，否则 EOS 标志会让读取循环不再启动
        mIsEOS = false;

        ALOGD("VEDemux::onSeek Successful to posMs: %f", posMs);
        ALOGV("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEDemux::putPacket(std::shared_ptr<VEPacket> packet, bool isAudio) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        // 只在 demux 自己的 looper 线程上执行，无需加锁
        std::shared_ptr<VEPacketQueue> &queue = isAudio ? mAudioPacketQueue : mVideoPacketQueue;
        bool &needMore = isAudio ? mNeedAudioMore : mNeedVideoMore;
        std::shared_ptr<AMessage> &notify = isAudio ? mAudioNotify : mVideoNotify;

        if (!queue->put(packet)) {
            ALOGD("VEDemux::putPacket %s queue is full, stopping read.", isAudio ? "Audio" : "Video");
            mIsStart = false;
        } else if (needMore) {
            needMore = false;
            if (notify) {
                ALOGD("VEDemux::putPacket %s queue post notify", isAudio ? "Audio" : "Video");
                notify->post();
            }
        }
        ALOGV("VEDemux::%s exit", __FUNCTION__);
    }

    void VEDemux::needMorePacket(std::shared_ptr<AMessage> msg, int type) {
        // 由解码器线程调用：必须转成消息投递到 demux 自己的 looper，
        // 否则会与读取循环并发读写 mNeedXxxMore / mXxxNotify / mIsStart。
        ALOGV("VEDemux::%s enter type:%d", __FUNCTION__, type);
        auto needMsg = std::make_shared<AMessage>(kWhatNeedMore, shared_from_this());
        needMsg->setObject("notify", msg);
        needMsg->setInt32("type", type);
        needMsg->post();
    }

    VEResult VEDemux::onNeedMorePacket(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<void> tmp;
        if (!msg->findObject("notify", &tmp)) {
            ALOGW("VEDemux::onNeedMorePacket notify not found");
            return VE_INVALID_PARAMS;
        }
        int32_t type = 0;
        msg->findInt32("type", &type);

        if (type == 1) {
            mAudioNotify = std::static_pointer_cast<AMessage>(tmp);
            mNeedAudioMore = true;
        } else {
            mVideoNotify = std::static_pointer_cast<AMessage>(tmp);
            mNeedVideoMore = true;
        }

        if (!mIsStart && !mIsEOS) {
            mIsStart = true;
            ALOGI("VEDemux::onNeedMorePacket - Starting to read packets.");
            std::make_shared<AMessage>(kWhatRead, shared_from_this())->post();
        }
        return VE_OK;
    }

    VEResult
    VEDemux::postMessage(int32_t event, int32_t arg1, int32_t arg2, int64_t arg3, void *params) {
        std::shared_ptr<AMessage> msg = mNotifyEvent->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_DEMUX);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return VE_OK;
    }

    VEResult VEDemux::onPause() {
        // 读取循环已由 mIsStart 停下，这里不需要额外动作
        return VE_OK;
    }

    VEResult VEDemux::onStop() {
        // 停止读取并丢掉已缓存的包，避免下次 start 时吐出上一轮的残留数据
        mIsStart = false;
        mIsEOS = false;
        mNeedAudioMore = false;
        mNeedVideoMore = false;
        if (mAudioPacketQueue) mAudioPacketQueue->clear();
        if (mVideoPacketQueue) mVideoPacketQueue->clear();
        return VE_OK;
    }

    VEResult VEDemux::onFlush() {
        mIsEOS = false;
        mNeedAudioMore = false;
        mNeedVideoMore = false;
        if (mAudioPacketQueue) mAudioPacketQueue->clear();
        if (mVideoPacketQueue) mVideoPacketQueue->clear();
        return VE_OK;
    }

    VEResult VEDemux::onRelease() {
        mIsStart = false;
        mAudioNotify.reset();
        mVideoNotify.reset();
        if (mAudioPacketQueue) mAudioPacketQueue->clear();
        if (mVideoPacketQueue) mVideoPacketQueue->clear();

        if (mFormatContext) {
            avformat_close_input(&mFormatContext);
            mFormatContext = nullptr;
        }

        if (mAudioCodecParams) {
            avcodec_parameters_free(&mAudioCodecParams);
            mAudioCodecParams = nullptr;
        }
        if (mVideoCodecParams) {
            avcodec_parameters_free(&mVideoCodecParams);
            mVideoCodecParams = nullptr;
        }
        return VE_OK;
    }
}