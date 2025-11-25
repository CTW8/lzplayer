//
// Created for lzplayer demux redesign following NuPlayerSource pattern
// VEGenericSource: Concrete implementation for local file playback
//

#include "VEGenericSource.h"
#include "Log.h"

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libavutil/dict.h"
}

namespace VE {

    VEGenericSource::VEGenericSource() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mAudioCodecParams = nullptr;
        mVideoCodecParams = nullptr;
        mFormatContext = nullptr;
        mAudioStartPts = -1;
        mVideoStartPts = -1;
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    VEGenericSource::~VEGenericSource() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        close();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    VEResult VEGenericSource::prepareAsync(const std::string& path) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setString("filePath", path);

        std::shared_ptr<AMessage> response;
        msg->postAndAwaitResponse(&response);
        int32_t ret;
        response->findInt32("ret", &ret);
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return ret;
    }

    void VEGenericSource::start() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    void VEGenericSource::stop() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    void VEGenericSource::pause() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    void VEGenericSource::resume() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatResume, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    VEResult VEGenericSource::read(bool isAudio, std::shared_ptr<VEPacket>& packet) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        ALOGD("VEGenericSource::read audio queue size: %d, video queue size: %d",
              mAudioPacketQueue->getDataSize(), mVideoPacketQueue->getDataSize());

        if (isAudio) {
            ALOGD("VEGenericSource::read mAudioPacketQueue size:%d", mAudioPacketQueue->getDataSize());
            if (mAudioPacketQueue->getDataSize() == 0) {
                ALOGD("VEGenericSource::read audio queue wait!!");
                ALOGI("VEGenericSource::%s exit", __FUNCTION__);
                return VE_NOT_ENOUGH_DATA;
            }
            packet = mAudioPacketQueue->get();
        } else {
            ALOGD("VEGenericSource::read mVideoPacketQueue size:%d", mVideoPacketQueue->getDataSize());
            if (mVideoPacketQueue->getDataSize() == 0) {
                ALOGD("VEGenericSource::read video queue wait!!");
                ALOGI("VEGenericSource::%s exit", __FUNCTION__);
                return VE_NOT_ENOUGH_DATA;
            }
            packet = mVideoPacketQueue->get();
        }

        // Resume reading if queues have space, we're not at EOS, and reading was stopped
        // (!mIsStarted means reading loop was stopped due to full queues)
        if (mVideoPacketQueue->getRemainingSize() > 0 && 
            mAudioPacketQueue->getRemainingSize() > 0 && 
            !mIsEOS && !mIsStarted) {
            std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
            msg->post();
        }

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::seekTo(int64_t posMs) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setInt64("posMs", posMs);
        std::shared_ptr<AMessage> response;
        msg->postAndAwaitResponse(&response);

        int32_t ret = VE_OK;
        response->findInt32("ret", &ret);
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return ret;
    }

    VEResult VEGenericSource::close() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
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

        mIsPrepared = false;
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    std::shared_ptr<VEMediaInfo> VEGenericSource::getMediaInfo() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<VEMediaInfo> info = std::make_shared<VEMediaInfo>();

        info->channels = mChannel;
        info->duration = mDuration;
        info->fps = mFps;
        info->width = mWidth;
        info->height = mHeight;
        info->sampleRate = mSampleRate;
        info->sampleFormat = mSampleFormat;
        info->mAudioCodecParams = mAudioCodecParams;
        info->mVideoCodecParams = mVideoCodecParams;
        info->audio_stream_index = mAudioIndex;
        info->video_stream_index = mVideoIndex;
        info->mAStartTime = mAStartTime;
        info->mAudioTimeBase = mAudioTimeBase;
        info->mVideoTimeBase = mVideoTimeBase;
        info->mVStartTime = mVStartTime;

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return info;
    }

    int64_t VEGenericSource::getDuration() {
        return static_cast<int64_t>(mDuration);
    }

    uint32_t VEGenericSource::getFlags() {
        // Local files support all seek operations
        return FLAG_CAN_PAUSE | FLAG_CAN_SEEK | FLAG_CAN_SEEK_BACKWARD | FLAG_CAN_SEEK_FORWARD;
    }

    void VEGenericSource::requestMoreData(std::shared_ptr<AMessage> msg, int type) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        if (type == 1) {
            mAudioNotify = msg;
            mNeedAudioMore = true;
            ALOGI("VEGenericSource::requestMoreData - Need more packets for audio.");
        } else {
            mVideoNotify = msg;
            mNeedVideoMore = true;
            ALOGI("VEGenericSource::requestMoreData - Need more packets for video.");
        }

        if (!mIsStarted) {
            mIsStarted = true;
            ALOGI("VEGenericSource::requestMoreData - Starting to read packets.");
            std::make_shared<AMessage>(kWhatRead, shared_from_this())->post();
        }
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    void VEGenericSource::onMessageReceived(const std::shared_ptr<AMessage>& msg) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        switch (msg->what()) {
            case kWhatPrepare: {
                std::string path;
                msg->findString("filePath", path);
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);
                VEResult ret = onPrepare(path);

                std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
                replyMsg->setInt32("ret", ret);
                replyMsg->postReply(replyToken);
                break;
            }
            case kWhatStart: {
                mIsStarted = true;
                onStart();
                break;
            }
            case kWhatStop: {
                mIsStarted = false;
                onStop();
                break;
            }
            case kWhatPause: {
                mIsStarted = false;
                onPause();
                break;
            }
            case kWhatResume: {
                mIsStarted = true;
                onResume();
                break;
            }
            case kWhatSeek: {
                int64_t pos = 0;
                msg->findInt64("posMs", &pos);
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);

                int32_t ret = onSeek(pos);

                std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
                replyMsg->setInt32("ret", ret);
                replyMsg->postReply(replyToken);
                break;
            }
            case kWhatRead: {
                if (!mIsStarted) {
                    ALOGD("VEGenericSource::%s kWhatRead !mIsStarted not run!!!", __FUNCTION__);
                    break;
                }
                ALOGD("VEGenericSource::%s kWhatRead run", __FUNCTION__);
                if (onRead() == VE_OK) {
                    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatRead,
                                                                                   shared_from_this());
                    readMsg->post();
                }
                break;
            }
            case kWhatClose: {
                onClose();
                break;
            }
        }
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    VEResult VEGenericSource::onPrepare(const std::string& path) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        
        if (path.empty()) {
            ALOGE("VEGenericSource::%s open file failed - empty path", __FUNCTION__);
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mFilePath = path;

        // Open input file
        if (avformat_open_input(&mFormatContext, mFilePath.c_str(), nullptr, nullptr) != 0) {
            ALOGE("VEGenericSource::onPrepare Error: Couldn't open input file.");
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        // Find stream info
        if (avformat_find_stream_info(mFormatContext, nullptr) < 0) {
            ALOGE("VEGenericSource::onPrepare Error: Couldn't find stream information.");
            avformat_close_input(&mFormatContext);
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mDuration = mFormatContext->duration / 1000;

        // Parse streams
        for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
            AVStream* stream = mFormatContext->streams[i];

            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                mAudioIndex = static_cast<int>(i);
                mAudioTimeBase = stream->time_base;
                mAStartTime = stream->start_time;
                mAudioCodecParams = avcodec_parameters_alloc();
                avcodec_parameters_copy(mAudioCodecParams, stream->codecpar);
                mChannel = mAudioCodecParams->ch_layout.nb_channels;
                mSampleFormat = mAudioCodecParams->format;
                mSampleRate = mAudioCodecParams->sample_rate;
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                mVideoIndex = static_cast<int>(i);
                mVideoTimeBase = stream->time_base;
                mVStartTime = stream->start_time;
                mVideoCodecParams = avcodec_parameters_alloc();
                avcodec_parameters_copy(mVideoCodecParams, stream->codecpar);
                mWidth = mVideoCodecParams->width;
                mHeight = mVideoCodecParams->height;
                // Calculate fps safely, avoiding division by zero
                if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
                    mFps = static_cast<int32_t>(av_q2d(stream->r_frame_rate));
                }
            }
        }

        // Initialize packet queues
        mAudioPacketQueue = std::make_shared<VEPacketQueue>(AUDIO_QUEUE_SIZE);
        mVideoPacketQueue = std::make_shared<VEPacketQueue>(VIDEO_QUEUE_SIZE);

        mIsPrepared = true;

        // Notify that source is prepared
        notifyListener(kWhatPrepared);

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onStart() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mIsEOS = false;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onStop() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mIsStarted = false;
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onPause() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mIsStarted = false;
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onResume() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mIsStarted = true;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onRead() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);

        if (mAudioPacketQueue->getDataSize() >= AUDIO_QUEUE_SIZE) {
            ALOGD("VEGenericSource::onRead Audio queue is full, stopping read.");
            mIsStarted = false;
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_NO_MEMORY;
        }

        if (mVideoPacketQueue->getDataSize() >= VIDEO_QUEUE_SIZE) {
            ALOGD("VEGenericSource::onRead Video queue is full, stopping read.");
            mIsStarted = false;
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_NO_MEMORY;
        }

        std::shared_ptr<VEPacket> packet = std::make_shared<VEPacket>();
        if (!packet) {
            ALOGD("VEGenericSource::onRead Could not allocate VEPacket");
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_OK;
        }

        int ret = av_read_frame(mFormatContext, packet->getPacket());
        if (ret == AVERROR_EOF) {
            // End of stream reached
            ALOGI("VEGenericSource::onRead End of Stream (EOS) reached.");
            packet->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(packet, true);

            std::shared_ptr<VEPacket> videoPacket = std::make_shared<VEPacket>();
            videoPacket->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(videoPacket, false);
            mIsEOS = true;

            notifyListener(kWhatEOS);

            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_EOS;
        } else if (ret < 0) {
            ALOGE("VEGenericSource::onRead Error occurred: %s", av_err2str(ret));
            notifyListener(kWhatError);
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        // Convert timestamps to unified timebase
        int64_t pts = av_rescale_q(packet->getPacket()->pts,
                                   mFormatContext->streams[packet->getPacket()->stream_index]->time_base,
                                   AV_TIME_BASE_Q);
        int64_t dts = av_rescale_q(packet->getPacket()->dts,
                                   mFormatContext->streams[packet->getPacket()->stream_index]->time_base,
                                   AV_TIME_BASE_Q);

        if (packet->getPacket()->stream_index == mAudioIndex) {
            packet->setPacketType(E_PACKET_TYPE_AUDIO);
            if (mAudioStartPts == -1) {
                mAudioStartPts = pts;
            }

            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = packet->getPts();
            packet->getPacket()->dts = packet->getDts();
            ALOGD("VEGenericSource::onRead Audio packet pts: %" PRId64 ", dts: %" PRId64,
                  packet->getPts(), packet->getDts());
            putPacket(packet, true);
        } else if (packet->getPacket()->stream_index == mVideoIndex) {
            packet->setPacketType(E_PACKET_TYPE_VIDEO);
            if (mVideoStartPts == -1) {
                mVideoStartPts = pts;
            }

            packet->setPts(pts - mVideoStartPts);
            packet->setDts(dts);
            packet->getPacket()->pts = packet->getPts();
            packet->getPacket()->dts = packet->getDts();
            ALOGD("VEGenericSource::onRead Video packet pts: %" PRId64 ", dts: %" PRId64,
                  packet->getPts(), packet->getDts());
            putPacket(packet, false);
        } else {
            ALOGD("VEGenericSource::onRead Packet from unused stream");
        }

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onSeek(int64_t posMs) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        
        if (!mFormatContext) {
            ALOGE("VEGenericSource::onSeek Error: File not opened.");
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }

        // Check if we have a valid video stream to seek
        if (mVideoIndex < 0 || mVideoIndex >= static_cast<int>(mFormatContext->nb_streams)) {
            ALOGE("VEGenericSource::onSeek Error: Invalid video stream index.");
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }

        ALOGD("VEGenericSource::onSeek posMs: %" PRId64, posMs);

        // Convert milliseconds to target timestamp
        int64_t targetPts = posMs * 1000;

        // Target stream timebase
        AVRational timeBase = mFormatContext->streams[mVideoIndex]->time_base;

        // Convert target timestamp to stream timebase
        int64_t seekTarget = av_rescale_q(targetPts, AV_TIME_BASE_Q, timeBase);

        // Perform seek using avformat_seek_file
        int ret = avformat_seek_file(mFormatContext, mVideoIndex, INT64_MIN, seekTarget, INT64_MAX,
                                     AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            ALOGE("VEGenericSource::onSeek Error: Couldn't seek using avformat_seek_file.");
            ALOGI("VEGenericSource::%s exit", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        // Clear packet queues
        resetQueues();

        ALOGD("VEGenericSource::onSeek Successful to posMs: %" PRId64, posMs);

        notifyListener(kWhatSeekDone);

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onClose() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        close();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEGenericSource::putPacket(std::shared_ptr<VEPacket> packet, bool isAudio) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        
        if (!isAudio) {
            if (!mVideoPacketQueue->put(packet)) {
                ALOGD("VEGenericSource::putPacket Video queue is full, stopping read.");
                mIsStarted = false;
            } else {
                ALOGD("VEGenericSource::putPacket Video queue mNeedVideoMore:%d", mNeedVideoMore);
                std::lock_guard<std::mutex> lk(mMutexVideo);

                if (mNeedVideoMore) {
                    mNeedVideoMore = false;
                    if (mVideoNotify) {
                        ALOGD("VEGenericSource::putPacket Video queue post notify");
                        mVideoNotify->post();
                    }
                }
            }
        } else {
            if (!mAudioPacketQueue->put(packet)) {
                ALOGD("VEGenericSource::putPacket Audio queue is full, stopping read.");
                mIsStarted = false;
            } else {
                ALOGD("VEGenericSource::putPacket Audio queue mNeedAudioMore:%d", mNeedAudioMore);
                std::lock_guard<std::mutex> lk(mMutexAudio);
                if (mNeedAudioMore) {
                    mNeedAudioMore = false;
                    if (mAudioNotify) {
                        ALOGD("VEGenericSource::putPacket Audio queue post notify");
                        mAudioNotify->post();
                    }
                }
            }
        }
        
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    void VEGenericSource::resetQueues() {
        if (mAudioPacketQueue) {
            mAudioPacketQueue->clear();
        }
        if (mVideoPacketQueue) {
            mVideoPacketQueue->clear();
        }
    }

} // namespace VE
