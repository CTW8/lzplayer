//
// Created for lzplayer demux redesign following NuPlayerSource pattern
// VEGenericSource: Concrete implementation for local file playback
// Based on Android's NuPlayer::GenericSource design
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
        disconnect();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    // ============ Data Source and Preparation ============

    VEResult VEGenericSource::setDataSource(const std::string& path) {
        ALOGI("VEGenericSource::%s enter path=%s", __FUNCTION__, path.c_str());
        std::lock_guard<std::mutex> lock(mLock);
        
        if (path.empty()) {
            ALOGE("VEGenericSource::%s empty path", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }
        
        mFilePath = path;
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::prepareAsync() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());

        std::shared_ptr<AMessage> response;
        msg->postAndAwaitResponse(&response);
        int32_t ret;
        response->findInt32("ret", &ret);
        ALOGI("VEGenericSource::%s exit ret=%d", __FUNCTION__, ret);
        return ret;
    }

    // ============ Control Methods ============

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

    void VEGenericSource::disconnect() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        {
            std::lock_guard<std::mutex> lock(mLock);
            mIsDisconnecting = true;
            mStopRead = true;
        }

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

        mTracks.clear();
        mIsPrepared = false;
        mIsStarted = false;
        
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    // ============ Track Management ============

    size_t VEGenericSource::getTrackCount() const {
        std::lock_guard<std::mutex> lock(mLock);
        return mTracks.size();
    }

    VEResult VEGenericSource::getTrackInfo(size_t trackIndex, VETrackInfo* info) const {
        std::lock_guard<std::mutex> lock(mLock);
        
        if (trackIndex >= mTracks.size() || info == nullptr) {
            return VE_INVALID_PARAMS;
        }

        const Track& track = mTracks[trackIndex];
        info->trackIndex = static_cast<int32_t>(trackIndex);
        info->type = track.type;
        info->isDefault = (track.type == VETrackInfo::TRACK_TYPE_VIDEO && 
                          static_cast<ssize_t>(trackIndex) == mVideoTrackIndex) ||
                         (track.type == VETrackInfo::TRACK_TYPE_AUDIO && 
                          static_cast<ssize_t>(trackIndex) == mAudioTrackIndex);

        if (track.codecParams) {
            if (track.type == VETrackInfo::TRACK_TYPE_VIDEO) {
                info->width = track.codecParams->width;
                info->height = track.codecParams->height;
            } else if (track.type == VETrackInfo::TRACK_TYPE_AUDIO) {
                info->sampleRate = track.codecParams->sample_rate;
                info->channelCount = track.codecParams->ch_layout.nb_channels;
            }
        }

        return VE_OK;
    }

    ssize_t VEGenericSource::getSelectedTrack(VETrackInfo::TrackType type) const {
        std::lock_guard<std::mutex> lock(mLock);
        
        switch (type) {
            case VETrackInfo::TRACK_TYPE_VIDEO:
                return mVideoTrackIndex;
            case VETrackInfo::TRACK_TYPE_AUDIO:
                return mAudioTrackIndex;
            case VETrackInfo::TRACK_TYPE_SUBTITLE:
                return mSubtitleTrackIndex;
            default:
                return -1;
        }
    }

    VEResult VEGenericSource::selectTrack(size_t trackIndex, bool select) {
        std::lock_guard<std::mutex> lock(mLock);
        
        if (trackIndex >= mTracks.size()) {
            return VE_INVALID_PARAMS;
        }

        Track& track = mTracks[trackIndex];
        track.selected = select;

        if (select) {
            switch (track.type) {
                case VETrackInfo::TRACK_TYPE_VIDEO:
                    mVideoTrackIndex = static_cast<ssize_t>(trackIndex);
                    break;
                case VETrackInfo::TRACK_TYPE_AUDIO:
                    mAudioTrackIndex = static_cast<ssize_t>(trackIndex);
                    break;
                case VETrackInfo::TRACK_TYPE_SUBTITLE:
                    mSubtitleTrackIndex = static_cast<ssize_t>(trackIndex);
                    break;
                default:
                    break;
            }
        }

        return VE_OK;
    }

    // ============ Format and Duration ============

    VEResult VEGenericSource::getFormat(bool audio, std::shared_ptr<AMessage>* format) {
        std::lock_guard<std::mutex> lock(mLock);
        
        if (!mIsPrepared || format == nullptr) {
            return VE_INVALID_OPERATION;
        }

        *format = std::make_shared<AMessage>();
        
        if (audio) {
            if (mAudioTrackIndex < 0) {
                return VE_INVALID_OPERATION;
            }
            (*format)->setInt32("sample-rate", mSampleRate);
            (*format)->setInt32("channel-count", mChannel);
            (*format)->setString("mime", "audio/raw");
        } else {
            if (mVideoTrackIndex < 0) {
                return VE_INVALID_OPERATION;
            }
            (*format)->setInt32("width", mWidth);
            (*format)->setInt32("height", mHeight);
            (*format)->setInt32("frame-rate", mFps);
            (*format)->setString("mime", "video/raw");
        }

        return VE_OK;
    }

    std::shared_ptr<VEMediaInfo> VEGenericSource::getMediaInfo() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::lock_guard<std::mutex> lock(mLock);
        
        std::shared_ptr<VEMediaInfo> info = std::make_shared<VEMediaInfo>();

        info->channels = mChannel;
        info->duration = mDurationUs / 1000;  // Convert to milliseconds
        info->fps = mFps;
        info->width = mWidth;
        info->height = mHeight;
        info->sampleRate = mSampleRate;
        info->sampleFormat = mSampleFormat;
        info->mAudioCodecParams = mAudioCodecParams;
        info->mVideoCodecParams = mVideoCodecParams;
        info->audio_stream_index = static_cast<int>(mAudioTrackIndex);
        info->video_stream_index = static_cast<int>(mVideoTrackIndex);
        info->mAStartTime = mAStartTime;
        info->mAudioTimeBase = mAudioTimeBase;
        info->mVideoTimeBase = mVideoTimeBase;
        info->mVStartTime = mVStartTime;

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return info;
    }

    int64_t VEGenericSource::getDurationUs() {
        std::lock_guard<std::mutex> lock(mLock);
        return mDurationUs;
    }

    uint32_t VEGenericSource::getFlags() {
        // Local files support all seek operations
        return FLAG_CAN_PAUSE | FLAG_CAN_SEEK | FLAG_CAN_SEEK_BACKWARD | FLAG_CAN_SEEK_FORWARD;
    }

    // ============ Data Access ============

    VEResult VEGenericSource::dequeueAccessUnit(bool audio, std::shared_ptr<VEPacket>* accessUnit) {
        ALOGI("VEGenericSource::%s enter audio=%d", __FUNCTION__, audio);
        
        if (accessUnit == nullptr) {
            return VE_INVALID_PARAMS;
        }

        std::shared_ptr<VEPacketQueue> queue = audio ? mAudioPacketQueue : mVideoPacketQueue;
        
        if (!queue) {
            ALOGE("VEGenericSource::dequeueAccessUnit queue is null");
            return VE_INVALID_OPERATION;
        }

        ALOGD("VEGenericSource::dequeueAccessUnit queue size: %d", queue->getDataSize());

        if (queue->getDataSize() == 0) {
            ALOGD("VEGenericSource::dequeueAccessUnit queue empty, need more data");
            return VE_NOT_ENOUGH_DATA;
        }

        *accessUnit = queue->get();

        // Resume reading if queues have space and we're not at EOS
        if (mVideoPacketQueue && mAudioPacketQueue &&
            mVideoPacketQueue->getRemainingSize() > 0 && 
            mAudioPacketQueue->getRemainingSize() > 0 && 
            !mIsEOS && !mIsStarted) {
            std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
            msg->post();
        }

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEGenericSource::requestMoreData(std::shared_ptr<AMessage> msg, int type) {
        ALOGI("VEGenericSource::%s enter type=%d", __FUNCTION__, type);
        std::lock_guard<std::mutex> lock(mLock);
        
        if (type == 1) {  // Audio
            mAudioNotify = msg;
            mNeedAudioMore = true;
            ALOGI("VEGenericSource::requestMoreData - Need more packets for audio.");
        } else {  // Video
            mVideoNotify = msg;
            mNeedVideoMore = true;
            ALOGI("VEGenericSource::requestMoreData - Need more packets for video.");
        }

        if (!mIsStarted && mIsPrepared) {
            mIsStarted = true;
            ALOGI("VEGenericSource::requestMoreData - Starting to read packets.");
            std::make_shared<AMessage>(kWhatReadBuffer, shared_from_this())->post();
        }
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    // ============ Seek ============

    VEResult VEGenericSource::seekTo(int64_t seekTimeUs, int32_t mode) {
        ALOGI("VEGenericSource::%s enter seekTimeUs=%" PRId64 " mode=%d", __FUNCTION__, seekTimeUs, mode);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setInt64("seekTimeUs", seekTimeUs);
        msg->setInt32("mode", mode);
        
        std::shared_ptr<AMessage> response;
        msg->postAndAwaitResponse(&response);

        int32_t ret = VE_OK;
        response->findInt32("ret", &ret);
        ALOGI("VEGenericSource::%s exit ret=%d", __FUNCTION__, ret);
        return ret;
    }

    // ============ Buffering ============

    int64_t VEGenericSource::getBufferedPositionUs() {
        std::lock_guard<std::mutex> lock(mLock);
        return mBufferedPositionUs;
    }

    bool VEGenericSource::isBuffering() {
        std::lock_guard<std::mutex> lock(mLock);
        return mIsBuffering;
    }

    // ============ Message Handler ============

    void VEGenericSource::onMessageReceived(const std::shared_ptr<AMessage>& msg) {
        ALOGI("VEGenericSource::%s enter what=0x%x", __FUNCTION__, msg->what());
        
        switch (msg->what()) {
            case kWhatSetDataSource: {
                std::string path;
                msg->findString("path", path);
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);
                VEResult ret = onSetDataSource(path);

                std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
                replyMsg->setInt32("ret", ret);
                replyMsg->postReply(replyToken);
                break;
            }
            case kWhatPrepare: {
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);
                VEResult ret = onPrepare();

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
                int64_t seekTimeUs = 0;
                int32_t mode = SEEK_PREVIOUS_SYNC;
                msg->findInt64("seekTimeUs", &seekTimeUs);
                msg->findInt32("mode", &mode);
                
                std::shared_ptr<AReplyToken> replyToken;
                msg->senderAwaitsResponse(replyToken);

                int32_t ret = onSeek(seekTimeUs, mode);

                std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
                replyMsg->setInt32("ret", ret);
                replyMsg->postReply(replyToken);
                break;
            }
            case kWhatReadBuffer: {
                if (!mIsStarted || mStopRead) {
                    ALOGD("VEGenericSource::%s kWhatReadBuffer not started", __FUNCTION__);
                    break;
                }
                ALOGD("VEGenericSource::%s kWhatReadBuffer running", __FUNCTION__);
                if (onReadBuffer() == VE_OK) {
                    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatReadBuffer,
                                                                                   shared_from_this());
                    readMsg->post();
                }
                break;
            }
            case kWhatDisconnect: {
                onDisconnect();
                break;
            }
            default:
                ALOGW("VEGenericSource::%s unhandled message 0x%x", __FUNCTION__, msg->what());
                break;
        }
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    // ============ Internal Handlers ============

    VEResult VEGenericSource::onSetDataSource(const std::string& path) {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        std::lock_guard<std::mutex> lock(mLock);
        mFilePath = path;
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onPrepare() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        
        if (mFilePath.empty()) {
            ALOGE("VEGenericSource::%s file path is empty", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mIsPreparing = true;

        // Open input file
        if (avformat_open_input(&mFormatContext, mFilePath.c_str(), nullptr, nullptr) != 0) {
            ALOGE("VEGenericSource::onPrepare Error: Couldn't open input file.");
            mIsPreparing = false;
            return VE_UNKNOWN_ERROR;
        }

        // Find stream info
        if (avformat_find_stream_info(mFormatContext, nullptr) < 0) {
            ALOGE("VEGenericSource::onPrepare Error: Couldn't find stream information.");
            avformat_close_input(&mFormatContext);
            mIsPreparing = false;
            return VE_UNKNOWN_ERROR;
        }

        // Store duration in microseconds (matching NuPlayer)
        mDurationUs = mFormatContext->duration;

        // Initialize tracks
        initTracks();

        // Initialize packet queues
        mAudioPacketQueue = std::make_shared<VEPacketQueue>(AUDIO_QUEUE_SIZE);
        mVideoPacketQueue = std::make_shared<VEPacketQueue>(VIDEO_QUEUE_SIZE);

        mIsPrepared = true;
        mIsPreparing = false;
        mStopRead = false;

        // Notify that source is prepared
        notifyListener(kWhatPrepared);

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEGenericSource::initTracks() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        
        mTracks.clear();
        mVideoTrackIndex = -1;
        mAudioTrackIndex = -1;

        for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
            AVStream* stream = mFormatContext->streams[i];
            Track track;
            track.index = i;
            track.stream = stream;
            track.codecParams = stream->codecpar;
            track.selected = false;
            track.lastDequeuedTimeUs = 0;

            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                track.type = VETrackInfo::TRACK_TYPE_AUDIO;
                
                if (mAudioTrackIndex < 0) {
                    mAudioTrackIndex = static_cast<ssize_t>(mTracks.size());
                    track.selected = true;
                    
                    mAudioTimeBase = stream->time_base;
                    mAStartTime = stream->start_time;
                    mAudioCodecParams = avcodec_parameters_alloc();
                    avcodec_parameters_copy(mAudioCodecParams, stream->codecpar);
                    mChannel = mAudioCodecParams->ch_layout.nb_channels;
                    mSampleFormat = mAudioCodecParams->format;
                    mSampleRate = mAudioCodecParams->sample_rate;
                }
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                track.type = VETrackInfo::TRACK_TYPE_VIDEO;
                
                if (mVideoTrackIndex < 0) {
                    mVideoTrackIndex = static_cast<ssize_t>(mTracks.size());
                    track.selected = true;
                    
                    mVideoTimeBase = stream->time_base;
                    mVStartTime = stream->start_time;
                    mVideoCodecParams = avcodec_parameters_alloc();
                    avcodec_parameters_copy(mVideoCodecParams, stream->codecpar);
                    mWidth = mVideoCodecParams->width;
                    mHeight = mVideoCodecParams->height;
                    
                    // Calculate fps safely
                    if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
                        mFps = static_cast<int32_t>(av_q2d(stream->r_frame_rate));
                    }
                }
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                track.type = VETrackInfo::TRACK_TYPE_SUBTITLE;
            } else {
                track.type = VETrackInfo::TRACK_TYPE_UNKNOWN;
            }

            mTracks.push_back(track);
        }

        ALOGI("VEGenericSource::%s exit tracks=%zu video=%zd audio=%zd", 
              __FUNCTION__, mTracks.size(), mVideoTrackIndex, mAudioTrackIndex);
    }

    VEResult VEGenericSource::onStart() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mIsEOS = false;
        mStopRead = false;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatReadBuffer, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onStop() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        mIsStarted = false;
        mStopRead = true;
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
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatReadBuffer, shared_from_this());
        msg->post();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onReadBuffer() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);

        if (mAudioPacketQueue->getDataSize() >= AUDIO_QUEUE_SIZE) {
            ALOGD("VEGenericSource::onReadBuffer Audio queue is full, stopping read.");
            mIsStarted = false;
            return VE_NO_MEMORY;
        }

        if (mVideoPacketQueue->getDataSize() >= VIDEO_QUEUE_SIZE) {
            ALOGD("VEGenericSource::onReadBuffer Video queue is full, stopping read.");
            mIsStarted = false;
            return VE_NO_MEMORY;
        }

        std::shared_ptr<VEPacket> packet = std::make_shared<VEPacket>();
        if (!packet) {
            ALOGD("VEGenericSource::onReadBuffer Could not allocate VEPacket");
            return VE_OK;
        }

        int ret = av_read_frame(mFormatContext, packet->getPacket());
        if (ret == AVERROR_EOF) {
            // End of stream reached
            ALOGI("VEGenericSource::onReadBuffer End of Stream (EOS) reached.");
            packet->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(packet, true);

            std::shared_ptr<VEPacket> videoPacket = std::make_shared<VEPacket>();
            videoPacket->setPacketType(E_PACKET_TYPE_EOF);
            putPacket(videoPacket, false);
            mIsEOS = true;

            notifyListener(kWhatEOS);

            ALOGI("VEGenericSource::%s exit EOS", __FUNCTION__);
            return VE_EOS;
        } else if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ALOGE("VEGenericSource::onReadBuffer Error occurred: %s", errbuf);
            notifyListener(kWhatError);
            return VE_UNKNOWN_ERROR;
        }

        int streamIndex = packet->getPacket()->stream_index;
        
        // Convert timestamps to unified timebase (microseconds)
        int64_t pts = av_rescale_q(packet->getPacket()->pts,
                                   mFormatContext->streams[streamIndex]->time_base,
                                   AV_TIME_BASE_Q);
        int64_t dts = av_rescale_q(packet->getPacket()->dts,
                                   mFormatContext->streams[streamIndex]->time_base,
                                   AV_TIME_BASE_Q);

        // Update buffered position
        mBufferedPositionUs = pts;

        if (mAudioTrackIndex >= 0 && 
            streamIndex == static_cast<int>(mTracks[mAudioTrackIndex].index)) {
            packet->setPacketType(E_PACKET_TYPE_AUDIO);
            if (mAudioStartPts == -1) {
                mAudioStartPts = pts;
            }

            packet->setPts(pts);
            packet->setDts(dts);
            packet->getPacket()->pts = packet->getPts();
            packet->getPacket()->dts = packet->getDts();
            ALOGD("VEGenericSource::onReadBuffer Audio packet pts: %" PRId64 ", dts: %" PRId64,
                  packet->getPts(), packet->getDts());
            putPacket(packet, true);
        } else if (mVideoTrackIndex >= 0 && 
                   streamIndex == static_cast<int>(mTracks[mVideoTrackIndex].index)) {
            packet->setPacketType(E_PACKET_TYPE_VIDEO);
            if (mVideoStartPts == -1) {
                mVideoStartPts = pts;
            }

            packet->setPts(pts - mVideoStartPts);
            packet->setDts(dts);
            packet->getPacket()->pts = packet->getPts();
            packet->getPacket()->dts = packet->getDts();
            ALOGD("VEGenericSource::onReadBuffer Video packet pts: %" PRId64 ", dts: %" PRId64,
                  packet->getPts(), packet->getDts());
            putPacket(packet, false);
        } else {
            ALOGD("VEGenericSource::onReadBuffer Packet from unused stream %d", streamIndex);
        }

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEGenericSource::onSeek(int64_t seekTimeUs, int32_t mode) {
        ALOGI("VEGenericSource::%s enter seekTimeUs=%" PRId64, __FUNCTION__, seekTimeUs);
        
        if (!mFormatContext) {
            ALOGE("VEGenericSource::onSeek Error: File not opened.");
            return VE_INVALID_PARAMS;
        }

        // Stop reading during seek
        mStopRead = true;

        int streamIndex = -1;
        AVRational timeBase = AV_TIME_BASE_Q;

        // Prefer video stream for seeking
        if (mVideoTrackIndex >= 0) {
            streamIndex = static_cast<int>(mTracks[mVideoTrackIndex].index);
            timeBase = mFormatContext->streams[streamIndex]->time_base;
        } else if (mAudioTrackIndex >= 0) {
            streamIndex = static_cast<int>(mTracks[mAudioTrackIndex].index);
            timeBase = mFormatContext->streams[streamIndex]->time_base;
        }

        if (streamIndex < 0) {
            ALOGE("VEGenericSource::onSeek Error: No valid stream to seek.");
            mStopRead = false;
            return VE_INVALID_PARAMS;
        }

        ALOGD("VEGenericSource::onSeek seekTimeUs: %" PRId64, seekTimeUs);

        // Convert seek time to stream timebase
        int64_t seekTarget = av_rescale_q(seekTimeUs, AV_TIME_BASE_Q, timeBase);

        // Determine seek flags based on mode
        int flags = 0;
        switch (mode) {
            case SEEK_PREVIOUS_SYNC:
                flags = AVSEEK_FLAG_BACKWARD;
                break;
            case SEEK_NEXT_SYNC:
                flags = 0;
                break;
            case SEEK_CLOSEST_SYNC:
            case SEEK_CLOSEST:
                flags = AVSEEK_FLAG_ANY;
                break;
        }

        // Perform seek
        int ret = avformat_seek_file(mFormatContext, streamIndex, 
                                     INT64_MIN, seekTarget, INT64_MAX, flags);
        if (ret < 0) {
            ALOGE("VEGenericSource::onSeek Error: Couldn't seek.");
            mStopRead = false;
            return VE_UNKNOWN_ERROR;
        }

        // Clear packet queues
        resetQueues();
        
        // Reset PTS tracking
        mAudioStartPts = -1;
        mVideoStartPts = -1;

        mStopRead = false;

        ALOGD("VEGenericSource::onSeek Successful");

        notifyListener(kWhatSeekDone);

        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEGenericSource::onDisconnect() {
        ALOGI("VEGenericSource::%s enter", __FUNCTION__);
        disconnect();
        ALOGI("VEGenericSource::%s exit", __FUNCTION__);
    }

    // ============ Helper Methods ============

    void VEGenericSource::putPacket(std::shared_ptr<VEPacket> packet, bool isAudio) {
        ALOGI("VEGenericSource::%s enter isAudio=%d", __FUNCTION__, isAudio);
        
        if (!isAudio) {
            if (!mVideoPacketQueue->put(packet)) {
                ALOGD("VEGenericSource::putPacket Video queue is full, stopping read.");
                mIsStarted = false;
            } else {
                ALOGD("VEGenericSource::putPacket Video queue mNeedVideoMore:%d", mNeedVideoMore);
                std::lock_guard<std::mutex> lk(mLock);

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
                std::lock_guard<std::mutex> lk(mLock);
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

    void VEGenericSource::notifyBufferingUpdate(int32_t percentage) {
        std::shared_ptr<AMessage> notify = mNotify;
        if (notify != nullptr) {
            std::shared_ptr<AMessage> msg = notify->dup();
            msg->setInt32("what", kWhatBufferingUpdate);
            msg->setInt32("percentage", percentage);
            msg->post();
        }
    }

    VEResult VEGenericSource::readBuffer(bool audio, int64_t* timeUs, std::shared_ptr<VEPacket>* pkt) {
        // This method is for internal use when we need to read specific buffers
        return dequeueAccessUnit(audio, pkt);
    }

} // namespace VE
