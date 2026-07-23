#include "VEVideoDecoder.h"
#include "VEBundle.h"
#include <iostream>

/// 解码帧队列深度。帧现在是引用而非整帧拷贝，但仍占住解码器的缓冲池，
/// 不宜过深；够吸收渲染抖动即可。
#define  FRAME_QUEUE_MAX_SIZE 6

namespace VE {
    namespace {
        /// 渲染器按 3 平面 8bit YUV420 上传纹理，这两种格式可以直接送过去
        bool isDirectRenderable(int format) {
            return format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P;
        }
    }

    VEVideoDecoder::VEVideoDecoder(std::shared_ptr<AMessage> &notify)
            : mVideoCtx(nullptr),
              mMediaInfo(nullptr),
              mIsStarted(false),
              mNeedMoreData(false) {
        ALOGV("VEVideoDecoder::VEVideoDecoder enter");
        mNofityEvent = notify;
    }

    VEVideoDecoder::~VEVideoDecoder() {
        ALOGV("VEVideoDecoder::~VEVideoDecoder enter");
        // 不直接释放资源，统一在 release / onRelease 中处理
    }

    VEResult VEVideoDecoder::prepare(std::shared_ptr<IMediaSource> demux) {
        ALOGV("VEVideoDecoder::prepare enter");
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

    VEResult VEVideoDecoder::seekTo(double timestampMs) {
        ALOGV("VEVideoDecoder::seekTo enter timestampMs:%f", timestampMs);

        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp", timestampMs);
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::start() {
        ALOGV("VEVideoDecoder::start enter");
        auto msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::pause() {
        ALOGV("VEVideoDecoder::pause enter");
        auto msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::stop() {
        ALOGV("VEVideoDecoder::stop enter");
        auto msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::flush() {
        ALOGV("VEVideoDecoder::flush enter");
        auto msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
        ALOGV("VEVideoDecoder::readFrame enter");
        if (!mFrameQueue) {
            ALOGE("VEVideoDecoder::readFrame frame queue is not initialized");
            return VE_UNKNOWN_ERROR;
        }

        if (mFrameQueue->getDataSize() == 0) {
            ALOGV("VEVideoDecoder::readFrame VE_NOT_ENOUGH_DATA");
            return VE_NOT_ENOUGH_DATA;
        }

        frame = mFrameQueue->get();
        if (frame) {
            ALOGD("VEVideoDecoder::readFrame got one frame, type=%d", frame->getFrameType());
            if(mIsEOS == false && mIsStarted == false){
                auto decodeMsg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
                decodeMsg->post();
            }
            return VE_OK;
        }
        return VE_NOT_ENOUGH_DATA;
    }

    void VEVideoDecoder::needMoreFrame(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDecoder::needMoreFrame enter");
        auto needMoreMsg = std::make_shared<AMessage>(kWhatNeedMore, shared_from_this());
        needMoreMsg->setObject("notify", msg);
        needMoreMsg->post();
    }

    VEResult VEVideoDecoder::release() {
        ALOGV("VEVideoDecoder::release enter");
        auto msg = std::make_shared<AMessage>(kWhatUninit, shared_from_this());
        msg->post();
        return VE_OK;
    }

// 消息处理函数
    void VEVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEVideoDecoder::onMessageReceived enter, what=%c%c%c%c",
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
                // 走到这里说明排在前面的解码消息都已处理完，之后的解码消息会因
                // mIsStarted=false 被丢弃，可以安全地告知上层"已停止消费"
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
                    // flush/seek 之前投递的解码消息，丢弃
                    ALOGI("VEVideoDecoder::onDecode stale decode msg, epoch=%d cur=%d", epoch, mEpoch);
                    break;
                }
                if (!mIsStarted) {
                    ALOGV("VEVideoDecoder::onDecode is not started, exiting");
                    break;
                }

                VEResult ret = onDecode();
                if (ret == VE_OK) {
                    postDecode();
                } else {
                    ALOGI("VEVideoDecoder::onMessageReceived onDecode stopped, ret=%d", ret);
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
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE,0,0,0, nullptr);
                break;
            }
            default: {
                ALOGW("VEVideoDecoder::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEResult VEVideoDecoder::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDecoder::onPrepare enter");
        std::shared_ptr<void> tmp;
        if (!msg->findObject("demux", &tmp)) {
            ALOGE("VEVideoDecoder::onPrepare demux not found in message");
            return VE_INVALID_PARAMS;
        }

        mDemux = std::static_pointer_cast<IMediaSource>(tmp);
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
        ALOGV("VEVideoDecoder::onStart enter");
        if (mIsStarted) {
            ALOGI("VEVideoDecoder::onStart already started");
            return VE_OK;
        }
        mIsEOS = false;
        mIsStarted = true;
        postDecode();
        return VE_OK;
    }

    VEResult VEVideoDecoder::onPause() {
        ALOGV("VEVideoDecoder::onPause enter");
        mIsStarted = false;
        return VE_OK;
    }

    VEResult VEVideoDecoder::onStop() {
        ALOGV("VEVideoDecoder::onStop enter");
        mIsStarted = false;
        if (mVideoCtx) {
            avcodec_flush_buffers(mVideoCtx);
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onFlush() {
        ALOGV("VEVideoDecoder::onFlush enter");
        // 递增 epoch，使 flush 之前投递的解码消息全部失效
        ++mEpoch;
        mIsStarted = false;
        mIsEOS = false;
        mNeedMoreData = false;
        if (mVideoCtx) {
            avcodec_flush_buffers(mVideoCtx);
        }
        if (mFrameQueue) {
            mFrameQueue->clear();
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onSeek(double timestampMs) {
        ALOGV("VEVideoDecoder::onSeek enter timestampMs:%f", timestampMs);
        // seek 必须真正清空 codec 内部参考帧和已解出的帧，
        // 否则 seek 后会渲染出目标位置之前的残留画面
        onFlush();
        // demux 只能定位到关键帧，这里记录目标位置，解码时丢弃其之前的帧
        mSeekTargetUs = static_cast<int64_t>(timestampMs * 1000);
        return VE_OK;
    }

    std::shared_ptr<VEFrame> VEVideoDecoder::convertToYuv420p(const std::shared_ptr<VEFrame> &src) {
        AVFrame *in = src->getFrame();

        mSwsCtx = sws_getCachedContext(mSwsCtx,
                                       in->width, in->height,
                                       static_cast<AVPixelFormat>(in->format),
                                       in->width, in->height, AV_PIX_FMT_YUV420P,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (mSwsCtx == nullptr) {
            ALOGE("VEVideoDecoder::%s failed to create sws context for format %d",
                  __FUNCTION__, in->format);
            return nullptr;
        }

        auto dst = std::make_shared<VEFrame>(in->width, in->height, AV_PIX_FMT_YUV420P);
        AVFrame *out = dst->getFrame();
        if (out == nullptr || out->data[0] == nullptr) {
            ALOGE("VEVideoDecoder::%s failed to allocate target frame", __FUNCTION__);
            return nullptr;
        }

        sws_scale(mSwsCtx, in->data, in->linesize, 0, in->height, out->data, out->linesize);

        dst->setFrameType(E_FRAME_TYPE_VIDEO);
        dst->setPts(in->pts);
        dst->setDts(in->pkt_dts);
        return dst;
    }

    void VEVideoDecoder::postDecode() {
        auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        decodeMsg->setInt32("epoch", mEpoch);
        decodeMsg->post();
    }

    VEResult VEVideoDecoder::onDecode() {
        ALOGV("VEVideoDecoder::onDecode enter");

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
                mIsEOS = true;
                return VE_EOS;
            }
            if (ret >= 0) {
                // 精准 seek：解码器从关键帧开始出帧，目标之前的帧直接丢弃，
                // 不做 YUV 拷贝也不入队，只是继续解码
                if (mSeekTargetUs != kNoSeekTarget) {
                    int64_t pts = frame->getFrame()->pts;
                    if (pts != AV_NOPTS_VALUE && pts < mSeekTargetUs) {
                        ALOGD("VEVideoDecoder::onDecode drop frame pts=%" PRId64
                                      " until %" PRId64, pts, mSeekTargetUs);
                        return VE_OK;
                    }
                    ALOGI("VEVideoDecoder::onDecode reached seek target pts=%" PRId64, pts);
                    mSeekTargetUs = kNoSeekTarget;
                }

                AVFrame *decoded = frame->getFrame();

                if (isDirectRenderable(decoded->format)) {
                    // 零拷贝：直接把解码出的帧交给渲染链路。渲染器用
                    // GL_UNPACK_ROW_LENGTH 处理行距，不需要先拷成紧排布。
                    frame->setFrameType(E_FRAME_TYPE_VIDEO);
                    frame->setPts(decoded->pts);
                    frame->setDts(decoded->pkt_dts);
                    ALOGD("VEVideoDecoder::onDecode got a frame: pts=%" PRId64, decoded->pts);
                    queueFrame(frame);
                    return VE_OK;
                }

                // 其它像素格式渲染器认不了，转成 YUV420P 再送
                auto videoFrame = convertToYuv420p(frame);
                if (videoFrame == nullptr) {
                    return VE_UNKNOWN_ERROR;
                }
                queueFrame(videoFrame);
                return VE_OK;
            }
            if (ret != AVERROR(EAGAIN)) {
                // 持续性错误(如 codec 未打开返回 EINVAL)：必须退出，
                // 否则循环条件不变化，忙循环卡死整个解码 looper
                ALOGE("VEVideoDecoder::onDecode receive_frame fatal: %d", ret);
                return VE_UNKNOWN_ERROR;
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
        ALOGV("VEVideoDecoder::onRelease enter");
        mIsStarted = false;
        if (mSwsCtx) {
            sws_freeContext(mSwsCtx);
            mSwsCtx = nullptr;
        }
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
        ALOGV("VEVideoDecoder::onNeedMoreFrame enter");
        std::shared_ptr<void> tmp;
        if (!msg->findObject("notify", &tmp)) {
            ALOGW("VEVideoDecoder::onNeedMoreFrame notify not found in message");
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

    void VEVideoDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
        ALOGV("VEVideoDecoder::queueFrame enter");
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

    VEResult VEVideoDecoder::postMessage(int32_t event, int32_t arg1, int32_t arg2, int64_t arg3,
                                         void *params) {
        std::shared_ptr<AMessage> msg = mNofityEvent->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return 0;
    }

}