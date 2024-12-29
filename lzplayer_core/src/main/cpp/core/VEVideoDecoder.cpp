#include "VEVideoDecoder.h"
#define FRAME_QUEUE_MAX_SIZE 3
#include "libyuv.h"

void ConvertYUV420PWithStrideToContinuous(const uint8_t* src_y, int src_stride_y,
                                          const uint8_t* src_u, int src_stride_u,
                                          const uint8_t* src_v, int src_stride_v,
                                          uint8_t* dst_y, uint8_t* dst_u, uint8_t* dst_v,
                                          int width, int height) {
    ALOGI("ConvertYUV420PWithStrideToContinuous enter");
    libyuv::CopyPlane(src_y, src_stride_y, dst_y, width, width, height);
    libyuv::CopyPlane(src_u, src_stride_u, dst_u, width / 2, width / 2, height / 2);
    libyuv::CopyPlane(src_v, src_stride_v, dst_v, width / 2, width / 2, height / 2);
}

VEVideoDecoder::VEVideoDecoder(){
    ALOGI("VEVideoDecoder::VEVideoDecoder enter");
    mVideoCtx = nullptr;
    mMediaInfo = nullptr;
    mIsStarted = false;
    mNeedMoreData = false;
}

VEVideoDecoder::~VEVideoDecoder() {
    ALOGI("VEVideoDecoder::~VEVideoDecoder enter");
}

void VEVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VEVideoDecoder::onMessageReceived enter");
    switch (msg->what()) {
        case kWhatInit: {
            onInit(msg);
            break;
        }
        case kWhatStart: {
            onStart();
            break;
        }
        case kWhatFlush: {
            onFlush();
            break;
        }
        case kWhatResume: {
            onResume();
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
        case kWhatDecode: {
            if (onDecode() == VE_OK) {
                std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
                readMsg->post();
            }
            break;
        }
        case kWhatUninit: {
            onUninit();
            break;
        }
        default:{
            break;
        }
    }
}

VEResult VEVideoDecoder::onInit(std::shared_ptr<AMessage> msg) {
    ALOGI("VEVideoDecoder::onInit enter");
    std::shared_ptr<void> tmp;
    msg->findObject("demux", &tmp);

    mFrameQueue = std::make_shared<VEFrameQueue>(FRAME_QUEUE_MAX_SIZE);


    mDemux = std::static_pointer_cast<VEDemux>(tmp);

    mMediaInfo = mDemux->getFileInfo();

    const AVCodec *video_codec = avcodec_find_decoder(mMediaInfo->mVideoCodecParams->codec_id);
    if (!video_codec) {
        ALOGE("VEVideoDecoder::onInit Could not find video codec");
        return VE_UNKNOWN_ERROR;
    }
    mVideoCtx = avcodec_alloc_context3(video_codec);
    if (!mVideoCtx) {
        ALOGE("VEVideoDecoder::onInit Could not allocate video codec context\n");
        return VE_UNKNOWN_ERROR;
    }
    if (avcodec_parameters_to_context(mVideoCtx, mMediaInfo->mVideoCodecParams) < 0) {
        ALOGE("VEVideoDecoder::onInit Could not copy codec parameters to codec context");
        avcodec_free_context(&mVideoCtx);
        return VE_UNKNOWN_ERROR;
    }
    if (avcodec_open2(mVideoCtx, video_codec, NULL) < 0) {
        fprintf(stderr, "VEVideoDecoder::onInit Could not open video codec\n");
        avcodec_free_context(&mVideoCtx);
        return VE_UNKNOWN_ERROR;
    }

    return VE_OK;
}

VEResult VEVideoDecoder::onStart() {
    ALOGI("VEVideoDecoder::onStart enter");
    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
    readMsg->post();
    return VE_OK;
}

VEResult VEVideoDecoder::onStop() {
    ALOGI("VEVideoDecoder::onStop enter");

    avcodec_flush_buffers(mVideoCtx);

    mIsStarted = false;

    return VE_OK;
}

VEResult VEVideoDecoder::onFlush() {
    ALOGI("VEVideoDecoder::onFlush enter");

    avcodec_flush_buffers(mVideoCtx);

    mFrameQueue->clear();

    return VE_OK;
}

VEResult VEVideoDecoder::onDecode() {
    ALOGI("VEVideoDecoder::onDecode enter");
    if (!mIsStarted) {
        ALOGI("VEVideoDecoder::onDecode stop requested, exiting");
        return VE_UNKNOWN_ERROR;
    }

    if (mFrameQueue->getRemainingSize() == 0) {
        ALOGI("VEVideoDecoder::onDecode frame queue is full, stopping decode");
        mIsStarted = false;
        return VE_UNKNOWN_ERROR;
    }

    std::shared_ptr<VEPacket> packet;
    VEResult ret = mDemux->read(false, packet);
    if(ret == VE_NOT_ENOUGH_DATA){
        ALOGI("VEVideoDecoder::onDecode not enough data");
//        mIsStarted = false;
        mDemux->needMorePacket(std::make_shared<AMessage>(kWhatDecode,shared_from_this()),2);
        return VE_NOT_ENOUGH_DATA;
    }

    if(packet == nullptr){
        return VE_UNKNOWN_ERROR;
    }

    if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
        ret = avcodec_send_packet(mVideoCtx, nullptr);
    } else {
        ALOGI("VEVideoDecoder::onDecode send packet, size: %d", packet->getPacket()->size);
        ret = avcodec_send_packet(mVideoCtx, packet->getPacket());
    }

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ALOGE("VEVideoDecoder::onDecode Error sending packet for decoding: %s", errbuf);
        return false;
    }
    while (ret >= 0) {
        std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
        ret = avcodec_receive_frame(mVideoCtx, frame->getFrame());
        if (ret == AVERROR(EAGAIN)) {
            ret = VE_OK;
            break;
        } else if (ret == AVERROR_EOF) {
            frame->setFrameType(E_FRAME_TYPE_EOF);
            queueFrame(frame);
            ret = VE_UNKNOWN_ERROR;
            break;
        } else if (ret < 0) {
            ALOGE("VEVideoDecoder::onDecode Error during decoding %d", ret);
            break;
        }

        std::shared_ptr<VEFrame> videoFrame = std::make_shared<VEFrame>(frame->getFrame()->width, frame->getFrame()->height, AV_PIX_FMT_YUV420P);
        videoFrame->setFrameType(E_FRAME_TYPE_VIDEO);
        ConvertYUV420PWithStrideToContinuous(frame->getFrame()->data[0], frame->getFrame()->linesize[0],
                                             frame->getFrame()->data[1], frame->getFrame()->linesize[1],
                                             frame->getFrame()->data[2], frame->getFrame()->linesize[2],
                                             videoFrame->getFrame()->data[0], videoFrame->getFrame()->data[1], videoFrame->getFrame()->data[2],
                                             frame->getFrame()->width, frame->getFrame()->height);

        videoFrame->setPts(frame->getFrame()->pts);
        videoFrame->setDts(frame->getFrame()->pkt_dts);
        ALOGD("VEVideoDecoder::onDecode video frame pts:%" PRId64 ", dts:%" PRId64,
              videoFrame->getPts(), videoFrame->getDts());
        queueFrame(videoFrame);
        ret = VE_OK;
    }
    ALOGI("VEVideoDecoder::onDecode exit");
    return ret;
}

VEResult VEVideoDecoder::onUninit() {
    ALOGI("VEVideoDecoder::onUninit enter");
    if (mVideoCtx) {
        avcodec_free_context(&mVideoCtx);
    }
    return false;
}

VEResult VEVideoDecoder::init(std::shared_ptr<VEDemux> demux) {
    ALOGI("VEVideoDecoder::init enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
    msg->setObject("demux", demux);
    msg->post();
    return 0;
}

void VEVideoDecoder::start() {
    ALOGI("VEVideoDecoder::start enter");
    mIsStarted = true;
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
    msg->post();
}

void VEVideoDecoder::stop() {
    ALOGI("VEVideoDecoder::stop enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
    msg->post();
}

VEResult VEVideoDecoder::flush() {
    ALOGI("VEVideoDecoder::flush enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
    msg->post();
    return 0;
}

VEResult VEVideoDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
    ALOGI("VEVideoDecoder::readFrame enter");
    if (mFrameQueue->getDataSize() == 0) {
        ALOGI("VEVideoDecoder::readFrame VE_NOT_ENOUGH_DATA");
        return VE_NOT_ENOUGH_DATA; // 队列为空，返回错误
    }

    frame = mFrameQueue->get();
    ALOGD("VEVideoDecoder::readFrame get one frame");
    return 0;
}

VEResult VEVideoDecoder::uninit() {
    ALOGI("VEVideoDecoder::uninit enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit, shared_from_this());
    msg->post();
    return 0;
}

void VEVideoDecoder::queueFrame(std::shared_ptr<VEFrame> videoFrame) {
    ALOGI("VEVideoDecoder::queueFrame enter");
    if (!mFrameQueue->put(videoFrame)) {
        ALOGD("VEVideoDecoder::queueFrame Frame queue is full, stopping decode.");
        mIsStarted = false;
    }else{
        std::lock_guard<std::mutex> lk(mMutex);
        if(mNeedMoreData){
            mNeedMoreData = false;
            mNotifyMore->post();
        }
    }
}

VEResult VEVideoDecoder::onResume() {
    mIsStarted = true;
    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
    readMsg->post();
    return 0;
}

VEResult VEVideoDecoder::onPause() {
    mIsStarted = false;
    return 0;
}

void VEVideoDecoder::needMoreFrame(std::shared_ptr<AMessage> msg) {
    std::unique_lock<std::mutex> lk(mMutex);
    mNotifyMore = msg;
    mNeedMoreData = true;
    ALOGI("VEVideoDecoder::needMoreFrame - Need more frames, notifying. mIsStarted: %d", mIsStarted);
    if(!mIsStarted){
        mIsStarted = true;
        std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        readMsg->post();
    }
}
