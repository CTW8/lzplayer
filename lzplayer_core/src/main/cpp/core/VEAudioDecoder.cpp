#include "VEAudioDecoder.h"
#include "VEError.h"
#include "VEBundle.h"

#define AUDIO_FRAME_QUEUE_SIZE 50

#define AUDIO_TARGET_OUTPUT_FORMAT AV_SAMPLE_FMT_S16
#define AUDIO_TARGET_OUTPUT_SAMPLERATE 44100
#define AUDIO_TARGET_OUTPUT_CHANNELS 2
namespace VE {
    VEAudioDecoder::VEAudioDecoder() {
        mAudioCtx = nullptr;
        mMediaInfo = nullptr;
        mIsStarted = false;
        mSwrCtx = nullptr;
        mIsEOS = false;
    }

    VEAudioDecoder::~VEAudioDecoder() {
        release();
    }

    VEResult VEAudioDecoder::prepare(std::shared_ptr<VEDemux> demux) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("demux", demux);
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::flush() {
        ALOGI("VEAudioDecoder::flush enter");
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
        ALOGI("VEAudioDecoder::readFrame enter");
        if (mFrameQueue->getDataSize() == 0) {
            ALOGI("VEAudioDecoder::readFrame - Not enough data in the queue");
            return VE_NOT_ENOUGH_DATA; // 队列为空，返回错误
        }

        frame = mFrameQueue->get();
        if(mIsEOS == false && mIsStarted == false && mFrameQueue->getRemainingSize() > AUDIO_FRAME_QUEUE_SIZE/2){
            auto decodeMsg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
            decodeMsg->post();
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
        ALOGI("VEAudioDecoder::onMessageReceived enter, what=%c%c%c%c",
              (msg->what() & 0xFF),
              (msg->what() >> 8) & 0xFF,
              (msg->what() >> 16) & 0xFF,
              (msg->what() >> 24) & 0xFF);

        switch (msg->what()) {
            case kWhatInit: {
                onPrepare(msg);
                break;
            }
            case kWhatStart: {
                onStart();
                break;
            }
            case kWhatPause: {
                onPause();
                break;
            }
            case kWhatStop: {
                onStop();
                break;
            }
            case kWhatFlush: {
                onFlush();
                break;
            }
            case kWhatDecode: {
                VEResult ret = onDecode();
                if (ret == VE_OK) {
                    auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
                    decodeMsg->post();
                } else {
                    mIsStarted = false;
                }
                break;
            }
            case kWhatUninit: {
                onRelease();
                break;
            }
            case kWhatNeedMore: {
                onNeedMoreFrame(msg);
                break;
            }
            default: {
                ALOGW("VEAudioDecoder::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEResult VEAudioDecoder::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGI("VEAudioDecoder::%s enter", __FUNCTION__);
        std::shared_ptr<void> tmp;
        msg->findObject("demux", &tmp);

        mFrameQueue = std::make_shared<VEFrameQueue>(AUDIO_FRAME_QUEUE_SIZE);
        mDemux = std::static_pointer_cast<VEDemux>(tmp);
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
        ALOGI("VEAudioDecoder::%s enter", __FUNCTION__);
        avcodec_flush_buffers(mAudioCtx);
        mFrameQueue->clear();
        return false;
    }

    VEResult VEAudioDecoder::onDecode() {
        ALOGI("VEAudioDecoder::%s enter", __FUNCTION__);
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
                ALOGI("###VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64,
                      frame->getFrame()->pts, frame->getFrame()->pkt_dts);

                if (frame->getFrame()->format != (int32_t) AUDIO_TARGET_OUTPUT_FORMAT ||
                    frame->getFrame()->sample_rate != AUDIO_TARGET_OUTPUT_SAMPLERATE ||
                    frame->getFrame()->ch_layout.nb_channels != AUDIO_TARGET_OUTPUT_CHANNELS) {
                    if (mSwrCtx == nullptr) {
                        mSwrCtx = swr_alloc();

                        // 1. 准备 AVChannelLayout
                        AVChannelLayout ch_layout;
                        // 2. 用新版接口根据声道数填充结构体
                        av_channel_layout_default(&ch_layout,
                                                  frame->getFrame()->ch_layout.nb_channels);
                        // 3. 从结构体中取出 uint64_t 掩码并传给 swr 上下文
                        av_opt_set_int(mSwrCtx,
                                       "in_channel_layout",
                                       (int64_t)ch_layout.u.mask,
                                       0);

                        av_opt_set_int(mSwrCtx, "in_sample_rate", frame->getFrame()->sample_rate,
                                       0);
                        av_opt_set_sample_fmt(mSwrCtx, "in_sample_fmt",
                                              static_cast<AVSampleFormat>(frame->getFrame()->format),
                                              0);

                        // 用目标声道数初始化布局
                        av_channel_layout_default(&ch_layout, AUDIO_TARGET_OUTPUT_CHANNELS);
                        // 从结构体中提取 uint64_t 掩码（注意转换为 int64_t 传给 av_opt_set_int）
                        auto layout_mask = (int64_t)ch_layout.u.mask;

                        av_opt_set_int(mSwrCtx,
                                       "out_channel_layout",
                                       layout_mask,
                                       0);

                        av_opt_set_int(mSwrCtx, "out_sample_rate", AUDIO_TARGET_OUTPUT_SAMPLERATE,
                                       0);
                        av_opt_set_sample_fmt(mSwrCtx, "out_sample_fmt", AUDIO_TARGET_OUTPUT_FORMAT,
                                              0);
                        if (swr_init(mSwrCtx) < 0) {
                            ALOGE("VEAudioDecoder Failed to initialize the resampling context");
                            swr_free(&mSwrCtx);
                            return -1;
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
                    ALOGI("@@@VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64 ", packet pts:%" PRId64 ", packet dts:%" PRId64,
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
                ALOGI("VEAudioDecoder Audio frame: pts=%s, nb_samples=%d, channels=%d samplerate:%d format:%d\n",
                      av_ts2str(frame->getFrame()->pts),
                      frame->getFrame()->nb_samples,
                      frame->getFrame()->ch_layout.nb_channels,
                      frame->getFrame()->sample_rate,
                      frame->getFrame()->format);
                return VE_OK;
            }
        } while (ret != AVERROR(EAGAIN));

        std::shared_ptr<VEPacket> packet;
        ret = mDemux->read(true, packet);
        if (ret == VE_NOT_ENOUGH_DATA) {
            ALOGI("VEAudioDecoder::onDecode not enough data");
            mDemux->needMorePacket(std::make_shared<AMessage>(kWhatDecode, shared_from_this()), 1);
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
        ALOGI("VEAudioDecoder::%s enter", __FUNCTION__);
        if (mAudioCtx) {
            avcodec_free_context(&mAudioCtx);
        }

        mMediaInfo = nullptr;
        return false;
    }

    VEResult VEAudioDecoder::onStart() {
        ALOGI("VEAudioDecoder::%s enter", __FUNCTION__);
        if (mIsStarted) {
            ALOGI("VEAudioDecoder::onStart already started");
            return VE_OK;
        }
        mIsStarted = true;
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        msg->post();
        return VE_OK;
    }

    VEResult VEAudioDecoder::onStop() {
        ALOGI("VEAudioDecoder::%s enter", __FUNCTION__);
        mIsStarted = false;

        avcodec_flush_buffers(mAudioCtx);

        mFrameQueue->clear();
        return false;
    }

    void VEAudioDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
        ALOGI("VEAudioDecoder::queueFrame enter");
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
        ALOGI("VEAudioDecoder::needMoreFrame enter");
        auto needMoreMsg = std::make_shared<AMessage>(kWhatNeedMore, shared_from_this());
        needMoreMsg->setObject("notify", msg);
        needMoreMsg->post();
    }

    VEResult VEAudioDecoder::onNeedMoreFrame(const std::shared_ptr<AMessage> &msg) {
        ALOGI("VEAudioDecoder::onNeedMoreFrame enter");
        std::shared_ptr<void> tmp;
        if (!msg->findObject("notify", &tmp)) {
            ALOGW("VEAudioDecoder::onNeedMoreFrame notify not found in message");
            return VE_INVALID_PARAMS;
        }

        auto notifyMsg = std::static_pointer_cast<AMessage>(tmp);

        mNotifyMore = notifyMsg;
        mNeedMoreData = true;

        if (!mIsStarted) {
            mIsStarted = true;
            auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
            decodeMsg->post();
        }
        return VE_OK;
    }

    VEResult VEAudioDecoder::prepare(VEBundle params) {
        return 0;
    }

    VEResult VEAudioDecoder::seekTo(double timestamp) {
        return flush();
    }
}