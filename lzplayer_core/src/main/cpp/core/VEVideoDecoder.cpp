#include "VEVideoDecoder.h"
#include "libyuv.h"
#include "VEBundle.h"
#include <iostream>

#define  FRAME_QUEUE_MAX_SIZE 3

namespace VE {
// 用于将 YUV420P 有步幅的数据复制到连续内存
    static void ConvertYUV420PWithStrideToContinuous(
            const uint8_t *src_y, int src_stride_y,
            const uint8_t *src_u, int src_stride_u,
            const uint8_t *src_v, int src_stride_v,
            uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
            int width, int height) {
        ALOGI("ConvertYUV420PWithStrideToContinuous enter");
        libyuv::CopyPlane(src_y, src_stride_y, dst_y, width, width, height);
        libyuv::CopyPlane(src_u, src_stride_u, dst_u, width / 2, width / 2, height / 2);
        libyuv::CopyPlane(src_v, src_stride_v, dst_v, width / 2, width / 2, height / 2);
    }

    VEVideoDecoder::VEVideoDecoder()
            : mVideoCtx(nullptr),
              mMediaInfo(nullptr),
              mIsStarted(false),
              mNeedMoreData(false) {
        ALOGI("VEVideoDecoder::VEVideoDecoder enter");
    }

    VEVideoDecoder::~VEVideoDecoder() {
        ALOGI("VEVideoDecoder::~VEVideoDecoder enter");
        // 不直接释放资源，统一在 release / onRelease 中处理
    }

    VEResult VEVideoDecoder::prepare(std::shared_ptr<VEDemux> demux) {
        ALOGI("VEVideoDecoder::prepare enter");
        if (!demux) {
            ALOGE("VEVideoDecoder::prepare demux is null");
            return VE_INVALID_PARAMS;
        }
        auto msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("demux", demux);
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::prepare(VEBundle params) {
        std::shared_ptr<VEDemux> demux = params.get<std::shared_ptr<VEDemux>>("demux");
        if (!demux) {
            ALOGE("VEVideoDecoder::prepare demux is null");
            return VE_INVALID_PARAMS;
        }
        auto msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("demux", demux);
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::seekTo(uint64_t timestamp) {
        return 0;
    }

    VEResult VEVideoDecoder::start() {
        ALOGI("VEVideoDecoder::start enter");
        auto msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::pause() {
        ALOGI("VEVideoDecoder::pause enter");
        auto msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::stop() {
        ALOGI("VEVideoDecoder::stop enter");
        auto msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::flush() {
        ALOGI("VEVideoDecoder::flush enter");
        auto msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
        ALOGI("VEVideoDecoder::readFrame enter");
        if (!mFrameQueue) {
            ALOGE("VEVideoDecoder::readFrame frame queue is not initialized");
            return VE_UNKNOWN_ERROR;
        }

        if (mFrameQueue->getDataSize() == 0) {
            ALOGI("VEVideoDecoder::readFrame VE_NOT_ENOUGH_DATA");
            return VE_NOT_ENOUGH_DATA;
        }

        frame = mFrameQueue->get();
        if (frame) {
            ALOGD("VEVideoDecoder::readFrame got one frame, type=%d", frame->getFrameType());
            return VE_OK;
        }
        return VE_NOT_ENOUGH_DATA;
    }

    void VEVideoDecoder::needMoreFrame(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoDecoder::needMoreFrame enter");
        auto needMoreMsg = std::make_shared<AMessage>(kWhatNeedMore, shared_from_this());
        needMoreMsg->setObject("notify", msg);
        needMoreMsg->post();
    }

    VEResult VEVideoDecoder::release() {
        ALOGI("VEVideoDecoder::release enter");
        auto msg = std::make_shared<AMessage>(kWhatUninit, shared_from_this());
        msg->post();
        return VE_OK;
    }

// 消息处理函数
    void VEVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGI("VEVideoDecoder::onMessageReceived enter, what=%c%c%c%c",
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
                if (!mIsStarted) {
                    ALOGI("VEVideoDecoder::onDecode is not started, exiting");
                    break;
                }

                VEResult ret = onDecode();
                if (ret == VE_OK) {
                    auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
                    decodeMsg->post();
                } else {
                    ALOGE("VEVideoDecoder::onMessageReceived onDecode failed, ret=%d", ret);
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
                ALOGW("VEVideoDecoder::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEResult VEVideoDecoder::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoDecoder::onPrepare enter");
        std::shared_ptr<void> tmp;
        if (!msg->findObject("demux", &tmp)) {
            ALOGE("VEVideoDecoder::onPrepare demux not found in message");
            return VE_INVALID_PARAMS;
        }

        mDemux = std::static_pointer_cast<VEDemux>(tmp);
        if (!mDemux) {
            ALOGE("VEVideoDecoder::onPrepare demux cast failed");
            return VE_INVALID_PARAMS;
        }

        mFrameQueue = std::make_shared<VEFrameQueue>(FRAME_QUEUE_MAX_SIZE);
        if (!mFrameQueue) {
            ALOGE("VEVideoDecoder::onPrepare failed to create frame queue");
            return VE_UNKNOWN_ERROR;
        }

        mMediaInfo = mDemux->getFileInfo();
        if (!mMediaInfo || !mMediaInfo->mVideoCodecParams) {
            ALOGE("VEVideoDecoder::onPrepare invalid media info or video codec params");
            return VE_INVALID_PARAMS;
        }

        const AVCodec *video_codec = avcodec_find_decoder(mMediaInfo->mVideoCodecParams->codec_id);
        if (!video_codec) {
            ALOGE("VEVideoDecoder::onPrepare Could not find video codec");
            return VE_UNKNOWN_ERROR;
        }

        mVideoCtx = avcodec_alloc_context3(video_codec);
        if (!mVideoCtx) {
            ALOGE("VEVideoDecoder::onPrepare Could not allocate video codec context");
            return VE_UNKNOWN_ERROR;
        }

        if (avcodec_parameters_to_context(mVideoCtx, mMediaInfo->mVideoCodecParams) < 0) {
            ALOGE("VEVideoDecoder::onPrepare Could not copy codec parameters to codec context");
            avcodec_free_context(&mVideoCtx);
            mVideoCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }

        if (avcodec_open2(mVideoCtx, video_codec, nullptr) < 0) {
            ALOGE("VEVideoDecoder::onPrepare Could not open video codec");
            avcodec_free_context(&mVideoCtx);
            mVideoCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }

        ALOGI("VEVideoDecoder::onPrepare success");
        return VE_OK;
    }

    VEResult VEVideoDecoder::onStart() {
        ALOGI("VEVideoDecoder::onStart enter");
        if (mIsStarted) {
            ALOGI("VEVideoDecoder::onStart already started");
            return VE_OK;
        }
        mIsStarted = true;
        auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        decodeMsg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::onPause() {
        ALOGI("VEVideoDecoder::onPause enter");
        mIsStarted = false;
        return VE_OK;
    }

    VEResult VEVideoDecoder::onStop() {
        ALOGI("VEVideoDecoder::onStop enter");
        mIsStarted = false;
        if (mVideoCtx) {
            avcodec_flush_buffers(mVideoCtx);
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onFlush() {
        ALOGI("VEVideoDecoder::onFlush enter");
        if (mVideoCtx) {
            avcodec_flush_buffers(mVideoCtx);
        }
        if (mFrameQueue) {
            mFrameQueue->clear();
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onDecode() {
        ALOGI("VEVideoDecoder::onDecode enter");

        if (mFrameQueue && mFrameQueue->getRemainingSize() <= 0) {
            ALOGI("VEVideoDecoder::onDecode frame queue is full");
            return VE_NOT_ENOUGH_DATA;
        }

        VEResult ret = VE_OK;
        do {
            auto frame = std::make_shared<VEFrame>();
            ret = avcodec_receive_frame(mVideoCtx, frame->getFrame());
            if (ret == AVERROR_EOF) {
                ALOGI("VEVideoDecoder::onDecode AVERROR_EOF");
                frame->setFrameType(E_FRAME_TYPE_EOF);
                queueFrame(frame);
                return VE_EOS;
            }
            if (ret >= 0) {
                auto videoFrame = std::make_shared<VEFrame>(
                        frame->getFrame()->width,
                        frame->getFrame()->height,
                        AV_PIX_FMT_YUV420P
                );
                videoFrame->setFrameType(E_FRAME_TYPE_VIDEO);
                ConvertYUV420PWithStrideToContinuous(
                        frame->getFrame()->data[0], frame->getFrame()->linesize[0],
                        frame->getFrame()->data[1], frame->getFrame()->linesize[1],
                        frame->getFrame()->data[2], frame->getFrame()->linesize[2],
                        videoFrame->getFrame()->data[0], videoFrame->getFrame()->data[1],
                        videoFrame->getFrame()->data[2],
                        frame->getFrame()->width, frame->getFrame()->height
                );

                videoFrame->setPts(frame->getFrame()->pts);
                videoFrame->setDts(frame->getFrame()->pkt_dts);
                ALOGD("VEVideoDecoder::onDecode got a frame: pts=%" PRId64 ", dts=%" PRId64,
                      videoFrame->getPts(), videoFrame->getDts());

                queueFrame(videoFrame);
                return VE_OK;
            }
        } while (ret != AVERROR(EAGAIN));

        std::shared_ptr<VEPacket> packet;
        ret = mDemux->read(false, packet);
        if (ret == VE_NOT_ENOUGH_DATA) {
            ALOGI("VEVideoDecoder::onDecode not enough data from demux");
            if (mDemux) {
                auto decodeMsg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
                mDemux->needMorePacket(decodeMsg, 2);
            }
            return VE_NOT_ENOUGH_DATA;
        }
        if (!packet) {
            ALOGI("VEVideoDecoder::onDecode packet is null");
            return VE_UNEXPECTED_NULL;
        }

        if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
            ALOGI("VEVideoDecoder::onDecode got EOF packet");
            ret = avcodec_send_packet(mVideoCtx, nullptr);
        } else {
            ALOGI("VEVideoDecoder::onDecode got normal packet, size=%d", packet->getPacket()->size);
            ret = avcodec_send_packet(mVideoCtx, packet->getPacket());
        }

        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ALOGE("VEVideoDecoder::onDecode Error sending packet: %s", errbuf);
            return VE_ERROR_EAGAIN;
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onRelease() {
        ALOGI("VEVideoDecoder::onRelease enter");
        if (mVideoCtx) {
            avcodec_free_context(&mVideoCtx);
            mVideoCtx = nullptr;
        }
        if (mFrameQueue) {
            mFrameQueue->clear();
            mFrameQueue.reset();
        }
        mMediaInfo.reset();
        mDemux.reset();
        mNotifyMore.reset();

        return VE_OK;
    }

    VEResult VEVideoDecoder::onNeedMoreFrame(const std::shared_ptr<AMessage> &msg) {
        ALOGI("VEVideoDecoder::onNeedMoreFrame enter");
        std::shared_ptr<void> tmp;
        if (!msg->findObject("notify", &tmp)) {
            ALOGW("VEVideoDecoder::onNeedMoreFrame notify not found in message");
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

    void VEVideoDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
        ALOGI("VEVideoDecoder::queueFrame enter");
        if (!mFrameQueue || !mFrameQueue->put(frame)) {
            ALOGI("VEVideoDecoder::queueFrame queue is full, stopping decode");
            mIsStarted = false;
        } else {
            if (mNeedMoreData && mNotifyMore) {
                mNeedMoreData = false;
                mNotifyMore->post();
            }
        }
    }

}