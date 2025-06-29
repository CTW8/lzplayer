#include "VEDemux.h"
#include "VEPacket.h"
#include "VEError.h"

extern "C"{
    #include"libavcodec/avcodec.h"
    #include"libavformat/avformat.h"
    #include"libavutil/dict.h"
}

#define AUDIO_QUEUE_SIZE    100
#define VIDEO_QUEUE_SIZE    100
namespace VE {
    VEDemux::VEDemux() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        mAudioCodecParams = nullptr;
        mVideoCodecParams = nullptr;
        mFormatContext = nullptr;
        mAudioStartPts = -1;
        mVideoStartPts = -1;
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    VEDemux::~VEDemux() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        close();
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    VEResult VEDemux::open(std::string file) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatOpen, shared_from_this());
        msg->setString("filePath", file);

        std::shared_ptr<AMessage> respone;
        msg->postAndAwaitResponse(&respone);
        int32_t ret;
        respone->findInt32("ret", &ret);
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return ret;
    }

    VEResult VEDemux::read(bool isAudio, std::shared_ptr<VEPacket> &packet) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        ALOGD("VEDemux::read audio queue size: %d, video queue size: %d",
              mAudioPacketQueue->getDataSize(), mVideoPacketQueue->getDataSize());
        if (isAudio) {
            ALOGD("VEDemux::read mAudioPacketQueue size:%d", mAudioPacketQueue->getDataSize());
            if (mAudioPacketQueue->getDataSize() == 0) {
                ALOGD("VEDemux::read audio queue wait!!");
                ALOGI("VEDemux::%s exit", __FUNCTION__);
                return VE_NOT_ENOUGH_DATA;
            }

            packet = mAudioPacketQueue->get();
        } else {
            ALOGD("VEDemux::read mVideoPacketQueue size:%d", mVideoPacketQueue->getDataSize());
            if (mVideoPacketQueue->getDataSize() == 0) {
                ALOGD("VEDemux::read video queue wait!!");
                ALOGI("VEDemux::%s exit", __FUNCTION__);
                return VE_NOT_ENOUGH_DATA;
            }

            packet = mVideoPacketQueue->get();
        }
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::close() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
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
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return 0;
    }

    std::shared_ptr<VEMediaInfo> VEDemux::getFileInfo() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
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
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return tmp;
    }

    void VEDemux::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        switch (msg->what()) {
            case kWhatOpen: {
                std::string path;
                msg->findString("filePath", path);
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);
                VEResult ret = onOpen(path);

                std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
                replyMsg->setInt32("ret", ret);
                replyMsg->postReply(replyToken);
                break;
            }
            case kWhatStart: {
                mIsStart = true;
                onStart();
                break;
            }
            case kWhatStop: {
                mIsStart = false;
                break;
            }
            case kWhatPause: {
                mIsStart = false;
                break;
            }
            case kWhatSeek: {
                double pos = 0;
                msg->findDouble("posMs", &pos);
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);

                int32_t ret = onSeek(pos);

                std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
                replyMsg->setInt32("ret", ret);
                replyMsg->postReply(replyToken);
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
        }
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    void VEDemux::start() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    void VEDemux::stop() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    void VEDemux::pause() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    VEResult VEDemux::seek(double posMs) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("posMs", posMs);
        std::shared_ptr<AMessage> response;
        msg->postAndAwaitResponse(&response);

        int32_t ret = VE_OK;
        response->findInt32("ret", &ret);
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return ret;
    }

    VEResult VEDemux::onOpen(std::string path) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        ///打开文件
        if (path.empty()) {
            printf("## %s  %d open file failed!!!", __FUNCTION__, __LINE__);
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mFilePath = path;

        if (avformat_open_input(&mFormatContext, mFilePath.c_str(), nullptr, nullptr) != 0) {
            fprintf(stderr, "Error: Couldn't open input file.\n");
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        // 获取流信息
        if (avformat_find_stream_info(mFormatContext, nullptr) < 0) {
            fprintf(stderr, "Error: Couldn't find stream information.\n");
            avformat_close_input(&mFormatContext);
            ALOGI("VEDemux::%s exit", __FUNCTION__);
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

        mAudioPacketQueue = std::make_shared<VEPacketQueue>(AUDIO_QUEUE_SIZE);
        mVideoPacketQueue = std::make_shared<VEPacketQueue>(VIDEO_QUEUE_SIZE);
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEDemux::onStart() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        mIsEOS = false;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead, shared_from_this());
        msg->post();
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEDemux::onRead() {
        ALOGI("VEDemux::%s enter", __FUNCTION__);

        if (mAudioPacketQueue->getDataSize() >= AUDIO_QUEUE_SIZE) {
            ALOGD("VEDemux::onRead Audio queue is full, stopping read.");
            mIsStart = false;
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_NO_MEMORY;
        }

        if (mVideoPacketQueue->getDataSize() >= VIDEO_QUEUE_SIZE) {
            ALOGD("VEDemux::onRead Video queue is full, stopping read.");
            mIsStart = false;
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_NO_MEMORY;
        }

        std::shared_ptr<VEPacket> packet = std::make_shared<VEPacket>();
        if (!packet) {
            ALOGD("VEDemux::onRead Could not allocate AVPacket");
            ALOGI("VEDemux::%s exit", __FUNCTION__);
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
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_EOS;
        } else if (ret < 0) {
            // 处理其他错误
            ALOGI("VEDemux::onRead Error occurred: %s", av_err2str(ret));
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        int64_t pts = av_rescale_q(packet->getPacket()->pts,
                                   mFormatContext->streams[packet->getPacket()->stream_index]->time_base,
                                   AV_TIME_BASE_Q);
        int64_t dts = av_rescale_q(packet->getPacket()->dts,
                                   mFormatContext->streams[packet->getPacket()->stream_index]->time_base,
                                   AV_TIME_BASE_Q);

        if (packet->getPacket()->stream_index == mAudio_index) {
            packet->setPacketType(E_PACKET_TYPE_AUDIO);
            if (mAudioStartPts == -1) {
                mAudioStartPts = pts;
            }

            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = packet->getPts();
            packet->getPacket()->dts = packet->getDts();
            ALOGD("VEDemux::onRead Audio packet pts (original): %" PRId64 ", converted: %" PRId64 " | dts (original): %" PRId64 ", converted: %" PRId64,
                  packet->getPacket()->pts, packet->getPts(), packet->getPacket()->dts,
                  packet->getDts());
            putPacket(packet, true);
        } else if (packet->getPacket()->stream_index == mVideo_index) {
            packet->setPacketType(E_PACKET_TYPE_VIDEO);
            if (mVideoStartPts == -1) {
                mVideoStartPts = pts;
            }

            packet->setPts(pts - mVideoStartPts);
            packet->setDts(dts);
            packet->getPacket()->pts = packet->getPts();
            packet->getPacket()->dts = packet->getDts();
            ALOGD("VEDemux::onRead Video packet pts (original): %" PRId64 ", converted: %" PRId64 " | dts (original): %" PRId64 ", converted: %" PRId64,
                  packet->getPacket()->pts, packet->getPts(), packet->getPacket()->dts,
                  packet->getDts());
            putPacket(packet, false);
        } else {
            ALOGD("VEDemux::onRead may be not use");
        }
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEDemux::onSeek(double posMs) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        if (!mFormatContext) {
            ALOGE("VEDemux::onSeek Error: File not opened.\n");
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }

        ALOGD("VEDemux::onSeek posMs:%f", posMs);

        // 将毫秒转换为目标时间戳
        int64_t targetPts = static_cast<int64_t>(posMs * 1000);

        // 目标流的时间基
        AVRational timeBase = mFormatContext->streams[mVideo_index]->time_base;

        // 目标时间戳转换到流时间基
        int64_t seekTarget = av_rescale_q(targetPts, AV_TIME_BASE_Q, timeBase);

        // 使用 avformat_seek_file 执行 Seek
        int ret = avformat_seek_file(mFormatContext, mVideo_index, INT64_MIN, seekTarget, INT64_MAX,
                                     AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            ALOGE("VEDemux::onSeek Error: Couldn't seek using avformat_seek_file.\n");
            ALOGI("VEDemux::%s exit", __FUNCTION__);
            return -1;
        }

        mAudioPacketQueue->clear();
        mVideoPacketQueue->clear();

        ALOGD("VEDemux::onSeek Successful to posMs: %f", posMs);
        ALOGI("VEDemux::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEDemux::putPacket(std::shared_ptr<VEPacket> packet, bool isAudio) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        if (!isAudio) {
            if (!mVideoPacketQueue->put(packet)) {
                ALOGD("VEDemux::putPacket Video queue is full, stopping read.");
                mIsStart = false;
            } else {
                ALOGD("VEDemux::putPacket Video queue mNeedVideoMore:%d", mNeedVideoMore);
                std::lock_guard<std::mutex> lk(mMutexVideo);

                if (mNeedVideoMore) {
                    mNeedVideoMore = false;
                    if (mVideoNotify) {
                        ALOGD("VEDemux::putPacket Video queue post notify");
                        mVideoNotify->post();
                    }
                }
            }
        } else {
            if (!mAudioPacketQueue->put(packet)) {
                ALOGD("VEDemux::putPacket Audio queue is full, stopping read.");
                mIsStart = false;
            } else {
                ALOGD("VEDemux::putPacket Audio queue mNeedAudioMore:%d", mNeedAudioMore);
                std::lock_guard<std::mutex> lk(mMutexAudio);
                if (mNeedAudioMore) {
                    mNeedAudioMore = false;
                    if (mAudioNotify) {
                        ALOGD("VEDemux::putPacket Video queue post notify");
                        mAudioNotify->post();
                    }
                }
            }
        }
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }

    void VEDemux::needMorePacket(std::shared_ptr<AMessage> msg, int type) {
        ALOGI("VEDemux::%s enter", __FUNCTION__);
        if (type == 1) {
            mAudioNotify = msg;
            mNeedAudioMore = true;
            ALOGI("VEDemux::needMorePacket - Need more packets for type audio.");
        } else {
            mVideoNotify = msg;
            mNeedVideoMore = true;
            ALOGI("VEDemux::needMorePacket - Need more packets for type: video.");
        }

        if (mIsStart == false) {
            mIsStart = true;
            ALOGI("VEDemux::needMorePacket - Starting to read packets.");
            std::make_shared<AMessage>(kWhatRead, shared_from_this())->post();
        }
        ALOGI("VEDemux::%s exit", __FUNCTION__);
    }
}