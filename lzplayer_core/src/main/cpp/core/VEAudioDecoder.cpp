#include "VEAudioDecoder.h"
#include "VEError.h"
#include "VEBundle.h"

#define AUDIO_FRAME_QUEUE_SIZE 50

// 输出格式不再写死：由 VEPlayer 依据源参数统一决定后传进来，
// 保证解码器重采样的目标和渲染器配置的设备参数是同一份。
#define AUDIO_TARGET_OUTPUT_FORMAT  ((AVSampleFormat) mOutFormat)
#define AUDIO_TARGET_OUTPUT_SAMPLERATE  mOutSampleRate
#define AUDIO_TARGET_OUTPUT_CHANNELS    mOutChannels

namespace VE {
    VEAudioDecoder::VEAudioDecoder(std::shared_ptr<AMessage> &notify) {
        mAudioCtx = nullptr;
        mMediaInfo = nullptr;
        mIsStarted = false;
        mSwrCtx = nullptr;
        mIsEOS = false;
        mNotifyEvent = notify;
    }

    VEAudioDecoder::~VEAudioDecoder() {
        // 不能调 release()：它内部要用 shared_from_this() 投递消息，
        // 而析构期间对象已不再被 shared_ptr 持有，会抛 bad_weak_ptr。
        // 正常路径下 onRelease 已由 release() 流程执行过，这里只是兜底。
        onRelease();
    }

    VEResult VEAudioDecoder::prepare(std::shared_ptr<IMediaSource> demux,
                                     const VEAudioOutputConfig &outConfig) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("demux", demux);
        // 随消息带过去，避免跨线程直接写解码器成员
        msg->setInt32("outSampleRate", outConfig.sampleRate);
        msg->setInt32("outChannels", outConfig.channels);
        msg->setInt32("outFormat", outConfig.format);
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::flush() {
        ALOGV("VEAudioDecoder::flush enter");
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
        ALOGV("VEAudioDecoder::readFrame enter");
        if (mFrameQueue == nullptr || mFrameQueue->getDataSize() == 0) {
            ALOGI("VEAudioDecoder::readFrame - Not enough data in the queue");
            return VE_NOT_ENOUGH_DATA; // 队列为空，返回错误
        }

        frame = mFrameQueue->get();
        if(mIsEOS == false && mIsStarted == false && mFrameQueue->getRemainingSize() > AUDIO_FRAME_QUEUE_SIZE/2){
            std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
        }
        return 0;
    }

    VEResult VEAudioDecoder::release() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::start() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::stop() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        return 0;
    }

    void VEAudioDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEAudioDecoder::onMessageReceived enter, what=%c%c%c%c",
              (msg->what() & 0xFF),
              (msg->what() >> 8) & 0xFF,
              (msg->what() >> 16) & 0xFF,
              (msg->what() >> 24) & 0xFF);

        switch (msg->what()) {
            case kWhatInit: {
                // prepare 失败必须上报：静默吞掉的话 codec 未打开，
                // start 后首次 receive_frame 就是持续性错误
                if (onPrepare(msg) != VE_OK) {
                    postMessage(VE_NOTIFY_EVENT_ERROR, VE_UNKNOWN_ERROR, 0, 0, nullptr);
                }
                break;
            }
            case kWhatStart: {
                onStart();
                break;
            }
            case kWhatPause: {
                onPause();
                postMessage(VE_NOTIFY_EVENT_PAUSE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatStop: {
                onStop();
                postMessage(VE_NOTIFY_EVENT_STOP_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatFlush: {
                onFlush();
                postMessage(VE_NOTIFY_EVENT_FLUSH_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatDecode: {
                int32_t epoch = 0;
                msg->findInt32("epoch", &epoch);
                if (epoch != mEpoch) {
                    ALOGI("VEAudioDecoder::onDecode stale decode msg, epoch=%d cur=%d", epoch, mEpoch);
                    break;
                }
                if (!mIsStarted) {
                    ALOGV("VEAudioDecoder::onDecode is not started, exiting");
                    break;
                }

                VEResult ret = onDecode();
                if (ret == VE_OK) {
                    postDecode();
                } else {
                    ALOGI("VEAudioDecoder::onMessageReceived onDecode stopped, ret=%d", ret);
                    mIsStarted = false;
                    if (ret != VE_NOT_ENOUGH_DATA && ret != VE_EOS && ret != VE_NO_MEMORY) {
                        postMessage(VE_NOTIFY_EVENT_ERROR, ret, 0, 0, nullptr);
                    }
                }
                break;
            }
            case kWhatUninit: {
                onRelease();
                postMessage(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatNeedMore: {
                onNeedMoreFrame(msg);
                break;
            }
            case kWhatSeek:{
                double timestampMs = 0;
                msg->findDouble("timestamp", &timestampMs);
                onSeek(timestampMs);
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE, 0, 0, 0, nullptr);
                break;
            }
            default: {
                ALOGW("VEAudioDecoder::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEResult VEAudioDecoder::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGV("VEAudioDecoder::%s enter", __FUNCTION__);
        std::shared_ptr<void> tmp;
        msg->findObject("demux", &tmp);

        msg->findInt32("outSampleRate", &mOutSampleRate);
        msg->findInt32("outChannels", &mOutChannels);
        msg->findInt32("outFormat", &mOutFormat);
        ALOGI("VEAudioDecoder::%s output config %dHz %dch fmt:%d", __FUNCTION__,
              mOutSampleRate, mOutChannels, mOutFormat);

        mFrameQueue = std::make_shared<VEFrameQueue>(AUDIO_FRAME_QUEUE_SIZE);
        mDemux = std::static_pointer_cast<IMediaSource>(tmp);
        std::shared_ptr<VEMediaInfo> info = mDemux->getFileInfo();
        if (info == nullptr) {
            return -1;
        }

        const AVCodec *codec = avcodec_find_decoder(info->mAudioCodecParams->codec_id);
        if (codec == nullptr) {
            return -1;
        }

        mAudioCtx = avcodec_alloc_context3(codec);
        if (mAudioCtx == nullptr) {
            return -1;
        }

        avcodec_parameters_to_context(mAudioCtx, info->mAudioCodecParams);

        if (avcodec_open2(mAudioCtx, codec, nullptr) != 0) {
            return -1;
        }
        return false;
    }

    VEResult VEAudioDecoder::onFlush() {
        ALOGV("VEAudioDecoder::%s enter", __FUNCTION__);
        // 递增 epoch，使 flush 之前投递的解码消息全部失效
        ++mEpoch;
        mIsStarted = false;
        mIsEOS = false;
        mNeedMoreData = false;
        // seek 流程会在 flush 之后重新设置；不经 seek 的 flush 必须清掉
        mSeekTargetUs = kNoSeekTarget;
        if (mAudioCtx) {
            avcodec_flush_buffers(mAudioCtx);
        }
        if (mFrameQueue) {
            mFrameQueue->clear();
        }
        return VE_OK;
    }

    VEResult VEAudioDecoder::onSeek(double timestampMs) {
        ALOGV("VEAudioDecoder::%s enter timestampMs:%f", __FUNCTION__, timestampMs);
        onFlush();
        mSeekTargetUs = static_cast<int64_t>(timestampMs * 1000);
        return VE_OK;
    }

    void VEAudioDecoder::postDecode() {
        auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        decodeMsg->setInt32("epoch", mEpoch);
        decodeMsg->post();
    }

    VEResult VEAudioDecoder::onDecode() {
        ALOGV("VEAudioDecoder::%s enter", __FUNCTION__);
        VEResult ret = VE_OK;
        if (mFrameQueue->getRemainingSize() == 0) {
            ALOGI("VEAudioDecoder::onDecode frame queue is full, stopping decode");
            return VE_NO_MEMORY;
        }

        do {
            std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
            ret = avcodec_receive_frame(mAudioCtx, frame->getFrame());
            if (ret == AVERROR_EOF) {
                ALOGI("VEAudioDecoder::onDecode AVERROR_EOF");
                frame->setFrameType(E_FRAME_TYPE_EOF);
                queueFrame(frame);
                mIsEOS = true;
                return VE_EOS;
            }

            if (ret >= 0) {
                ALOGV("###VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64,
                      frame->getFrame()->pts, frame->getFrame()->pkt_dts);

                // 精准 seek：丢弃目标位置之前的音频帧，避免 seek 后回放旧内容
                if (mSeekTargetUs != kNoSeekTarget) {
                    int64_t pts = frame->getFrame()->pts;
                    if (pts != AV_NOPTS_VALUE && pts < mSeekTargetUs) {
                        ALOGD("VEAudioDecoder::onDecode drop frame pts=%" PRId64
                                      " until %" PRId64, pts, mSeekTargetUs);
                        return VE_OK;
                    }
                    ALOGI("VEAudioDecoder::onDecode reached seek target pts=%" PRId64, pts);
                    mSeekTargetUs = kNoSeekTarget;
                }

                if (frame->getFrame()->format != (int32_t) AUDIO_TARGET_OUTPUT_FORMAT ||
                    frame->getFrame()->sample_rate != AUDIO_TARGET_OUTPUT_SAMPLERATE ||
                    frame->getFrame()->ch_layout.nb_channels != AUDIO_TARGET_OUTPUT_CHANNELS) {
                    if (mSwrCtx == nullptr) {
                        // 统一用新版 ch_layout 接口，不再混用已废弃的
                        // "in_channel_layout"/"out_channel_layout" 掩码选项
                        AVChannelLayout inLayout;
                        AVChannelLayout outLayout;
                        av_channel_layout_copy(&inLayout, &frame->getFrame()->ch_layout);
                        av_channel_layout_default(&outLayout, AUDIO_TARGET_OUTPUT_CHANNELS);

                        int ret = swr_alloc_set_opts2(
                                &mSwrCtx,
                                &outLayout, AUDIO_TARGET_OUTPUT_FORMAT, AUDIO_TARGET_OUTPUT_SAMPLERATE,
                                &inLayout,
                                static_cast<AVSampleFormat>(frame->getFrame()->format),
                                frame->getFrame()->sample_rate,
                                0, nullptr);

                        av_channel_layout_uninit(&inLayout);
                        av_channel_layout_uninit(&outLayout);

                        if (ret < 0 || mSwrCtx == nullptr) {
                            ALOGE("VEAudioDecoder failed to alloc resampling context: %d", ret);
                            return VE_UNKNOWN_ERROR;
                        }
                        if (swr_init(mSwrCtx) < 0) {
                            ALOGE("VEAudioDecoder Failed to initialize the resampling context");
                            swr_free(&mSwrCtx);
                            return VE_UNKNOWN_ERROR;
                        }
                    }

                    uint8_t **out_data = NULL;
                    swr_get_out_samples(mSwrCtx, frame->getFrame()->nb_samples);
                    int32_t out_nb_samples = swr_get_out_samples(mSwrCtx,
                                                                 frame->getFrame()->nb_samples);
                    int32_t out_samples_per_channel;
                    int32_t out_buffer_size;

                    av_samples_alloc_array_and_samples(&out_data, &out_buffer_size,
                                                       AUDIO_TARGET_OUTPUT_CHANNELS,
                                                       out_nb_samples, AUDIO_TARGET_OUTPUT_FORMAT,
                                                       0);
                    memset(out_data[0], 0, out_buffer_size);
                    out_samples_per_channel = swr_convert(mSwrCtx, out_data, out_nb_samples,
                                                          (const uint8_t **) frame->getFrame()->data,
                                                          frame->getFrame()->nb_samples);

                    if (out_samples_per_channel < 0) {
                        ALOGE("VEAudioDecoder swr_convert failed\n");
                        return -1;
                    }
                    std::shared_ptr<VEFrame> audioFrame = std::make_shared<VEFrame>(
                            AUDIO_TARGET_OUTPUT_SAMPLERATE, AUDIO_TARGET_OUTPUT_CHANNELS,
                            out_samples_per_channel, (int32_t) AUDIO_TARGET_OUTPUT_FORMAT);
                    ALOGI("VEAudioDecoder out_samples_per_channel:%d  len:%d  linesize:%d out_buffer_size:%d",
                          out_samples_per_channel,
                          out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS *
                          av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT),
                          audioFrame->getFrame()->linesize[0], out_buffer_size);

                    memcpy(audioFrame->getFrame()->data[0], out_data[0],
                           out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS *
                           av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT));
                    audioFrame->getFrame()->pts = frame->getFrame()->pts;
                    audioFrame->getFrame()->pkt_dts = frame->getFrame()->pkt_dts;
                    audioFrame->getFrame()->nb_samples = out_samples_per_channel;
                    audioFrame->getFrame()->linesize[0] =
                            out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS *
                            av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT);

                    audioFrame->setPts(audioFrame->getFrame()->pts);
                    audioFrame->setDts(audioFrame->getFrame()->pkt_dts);
                    ALOGV("@@@VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64 ", packet pts:%" PRId64 ", packet dts:%" PRId64,
                          audioFrame->getFrame()->pts, audioFrame->getFrame()->pkt_dts,
                          audioFrame->getPts(), audioFrame->getDts());
                    av_freep(&out_data[0]);
                    av_freep(&out_data);
                    audioFrame->setFrameType(E_FRAME_TYPE_AUDIO);
                    queueFrame(audioFrame);
                } else {
                    frame->setFrameType(E_FRAME_TYPE_AUDIO);
                    queueFrame(frame);
                }
                ALOGV("VEAudioDecoder Audio frame: pts=%s, nb_samples=%d, channels=%d samplerate:%d format:%d\n",
                      av_ts2str(frame->getFrame()->pts),
                      frame->getFrame()->nb_samples,
                      frame->getFrame()->ch_layout.nb_channels,
                      frame->getFrame()->sample_rate,
                      frame->getFrame()->format);
                return VE_OK;
            }
            if (ret != AVERROR(EAGAIN)) {
                // 持续性错误(如 codec 未打开返回 EINVAL)：必须退出，
                // 否则循环条件不变化，忙循环卡死整个解码 looper
                ALOGE("VEAudioDecoder::onDecode receive_frame fatal: %d", ret);
                return VE_UNKNOWN_ERROR;
            }
        } while (ret != AVERROR(EAGAIN));

        std::shared_ptr<VEPacket> packet;
        ret = mDemux->read(true, packet);
        if (ret == VE_NOT_ENOUGH_DATA) {
            ALOGV("VEAudioDecoder::onDecode not enough data");
            // 唤醒消息必须用 kWhatStart(与视频解码器一致)：此刻上层会把
            // mIsStarted 置 false，且 kWhatDecode 需要携带 epoch 才不会被
            // 当作过期消息丢弃——直接投 kWhatDecode 的唤醒必然被丢，
            // 造成音频链路永久停摆。onStart 会重新置位并按当前 epoch 续投。
            mDemux->needMorePacket(std::make_shared<AMessage>(kWhatStart, shared_from_this()), 1);
            return VE_NOT_ENOUGH_DATA;
        }

        if (packet == nullptr) {
            return VE_UNKNOWN_ERROR;
        }

        if (packet->getPacketType() == E_PACKET_TYPE_AUDIO) {
            ret = avcodec_send_packet(mAudioCtx, packet->getPacket());
            ALOGI("VEAudioDecoder::onDecode send packet pts:%" PRId64 ", dts:%" PRId64,
                  packet->getPacket()->pts, packet->getPacket()->dts);
        } else if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
            ret = avcodec_send_packet(mAudioCtx, nullptr);
        }

        if (ret < 0) {
            ALOGE("VEAudioDecoder Error sending packet for decoding %d", ret);
            return VE_UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    VEResult VEAudioDecoder::onRelease() {
        ALOGV("VEAudioDecoder::%s enter", __FUNCTION__);
        mIsStarted = false;
        if (mAudioCtx) {
            avcodec_free_context(&mAudioCtx);
            mAudioCtx = nullptr;
        }
        if (mSwrCtx) {
            swr_free(&mSwrCtx);
            mSwrCtx = nullptr;
        }
        if (mFrameQueue) {
            mFrameQueue->clear();
            mFrameQueue.reset();
        }
        mDemux.reset();
        mNotifyMore.reset();
        mMediaInfo = nullptr;
        return VE_OK;
    }

    VEResult VEAudioDecoder::onStart() {
        ALOGV("VEAudioDecoder::%s enter", __FUNCTION__);
        if (mIsStarted) {
            ALOGI("VEAudioDecoder::onStart already started");
            return VE_OK;
        }
        mIsEOS = false;
        mIsStarted = true;
        postDecode();
        return VE_OK;
    }

    VEResult VEAudioDecoder::onStop() {
        ALOGV("VEAudioDecoder::%s enter", __FUNCTION__);
        mIsStarted = false;
        // 残留的精准 seek 目标会让下次重播把 0~target 的帧全部丢掉
        mSeekTargetUs = kNoSeekTarget;

        // prepare 失败(codec 缺失)或 release 之后仍可能收到 stop，须判空
        if (mAudioCtx) {
            avcodec_flush_buffers(mAudioCtx);
        }
        if (mFrameQueue) {
            mFrameQueue->clear();
        }
        return VE_OK;
    }

    void VEAudioDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
        ALOGV("VEAudioDecoder::queueFrame enter");
        if (!mFrameQueue->put(frame)) {
            ALOGI("VEAudioDecoder::queueFrame queue is full, stopping decode");
            mIsStarted = false;
        } else {
            if (mNeedMoreData && mNotifyMore) {
                mNeedMoreData = false;
                mNotifyMore->post();
            }
        }
    }

    VEResult VEAudioDecoder::onPause() {
        mIsStarted = false;
        return 0;
    }

    VEResult VEAudioDecoder::pause() {
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        return 0;
    }

    void VEAudioDecoder::needMoreFrame(std::shared_ptr<AMessage> msg) {
        ALOGV("VEAudioDecoder::needMoreFrame enter");
        auto needMoreMsg = std::make_shared<AMessage>(kWhatNeedMore, shared_from_this());
        needMoreMsg->setObject("notify", msg);
        needMoreMsg->post();
    }

    VEResult VEAudioDecoder::onNeedMoreFrame(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEAudioDecoder::onNeedMoreFrame enter");
        std::shared_ptr<void> tmp;
        if (!msg->findObject("notify", &tmp)) {
            ALOGW("VEAudioDecoder::onNeedMoreFrame notify not found in message");
            return VE_INVALID_PARAMS;
        }

        auto notifyMsg = std::static_pointer_cast<AMessage>(tmp);

        mNotifyMore = notifyMsg;
        mNeedMoreData = true;

        if (!mIsStarted && !mIsEOS) {
            mIsStarted = true;
            postDecode();
        }
        return VE_OK;
    }

    VEResult VEAudioDecoder::prepare(VEBundle params) {
        return 0;
    }

    VEResult VEAudioDecoder::seekTo(double timestamp) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp",timestamp);
        msg->post();
        return VE_OK;
    }

    VEResult VEAudioDecoder::postMessage(int32_t event, int32_t arg1, int32_t arg2, int64_t arg3,
                                         void *params) {
        std::shared_ptr<AMessage> msg = mNotifyEvent->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return 0;
    }
}