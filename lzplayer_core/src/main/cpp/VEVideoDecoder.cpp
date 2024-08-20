#include"VEVideoDecoder.h"
#define FRAME_QUEUE_MAX_SIZE  3
#include "libyuv.h"
#include "Errors.h"

void ConvertYUV420PWithStrideToContinuous(const uint8_t* src_y, int src_stride_y,
                                          const uint8_t* src_u, int src_stride_u,
                                          const uint8_t* src_v, int src_stride_v,
                                          uint8_t* dst_y, uint8_t* dst_u, uint8_t* dst_v,
                                          int width, int height) {
    // 复制 Y 平面
    libyuv::CopyPlane(src_y, src_stride_y, dst_y, width, width, height);
    // 复制 U 平面
    libyuv::CopyPlane(src_u, src_stride_u, dst_u, width / 2, width / 2, height / 2);
    // 复制 V 平面
    libyuv::CopyPlane(src_v, src_stride_v, dst_v, width / 2, width / 2, height / 2);
}


VEVideoDecoder::VEVideoDecoder()
{
    mVideoCtx = nullptr;
    mMediaInfo = nullptr;
//    fp = fopen("/data/local/tmp/dump_dec.yuv","wb+");
}

VEVideoDecoder::~VEVideoDecoder()
{
//    if(fp){
//        fflush(fp);
//        fclose(fp);
//    }
}

void VEVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatInit:{
            onInit(msg);
            break;
        }
        case kWhatStart:{
            onStart();
            break;
        }
        case kWhatFlush:{
            onFlush();
            break;
        }
        case kWhatStop:{
            onStop();
            break;
        }
        case kWhatDecode:{
            if(onDecode() == OK){
                std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode,shared_from_this());
                readMsg->post();
            }
            break;
        }
        case kWhatUninit:{
            onUninit();
            break;
        }
    }
}

status_t VEVideoDecoder::onInit(std::shared_ptr<AMessage> msg) {
    ALOGI("VEVideoDecoder::%s",__FUNCTION__ );
    std::shared_ptr<void> tmp;
    msg->findObject("demux",&tmp);

    mDemux = std::static_pointer_cast<VEDemux>(tmp);

    mMediaInfo = mDemux->getFileInfo();

    const AVCodec *video_codec = avcodec_find_decoder(mMediaInfo->mVideoCodecParams->codec_id);
    if (!video_codec) {
        ALOGE("Could not find video codec");
        return UNKNOWN_ERROR;
    }
    mVideoCtx = avcodec_alloc_context3(video_codec);
    if (!mVideoCtx) {
        ALOGE("Could not allocate video codec context\n");
        return UNKNOWN_ERROR;
    }
    if (avcodec_parameters_to_context(mVideoCtx, mMediaInfo->mVideoCodecParams) < 0) {
        ALOGE("Could not copy codec parameters to codec context");
        avcodec_free_context(&mVideoCtx);
        return UNKNOWN_ERROR;
    }
    if (avcodec_open2(mVideoCtx, video_codec, NULL) < 0) {
        fprintf(stderr, "Could not open video codec\n");
        avcodec_free_context(&mVideoCtx);
        return UNKNOWN_ERROR;
    }

    return OK;
}

status_t VEVideoDecoder::onStart() {
    ALOGI("VEVideoDecoder::%s",__FUNCTION__ );
    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode,shared_from_this());
    readMsg->post();
    return OK;
}

status_t VEVideoDecoder::onStop() {
    ALOGI("VEVideoDecoder::%s",__FUNCTION__ );
    return OK;
}

status_t VEVideoDecoder::onFlush() {
    ALOGI("VEVideoDecoder::%s",__FUNCTION__ );
    return OK;
}

status_t VEVideoDecoder::onDecode() {
    ALOGI("VEVideoDecoder::%s enter",__FUNCTION__ );
    std::shared_ptr<VEPacket> packet;
    mDemux->read(false,packet);
    int ret =0;
    if(packet->getPacketType() == E_PACKET_TYPE_EOF){
        ret = avcodec_send_packet(mVideoCtx, nullptr);
    }else{
        ret = avcodec_send_packet(mVideoCtx, packet->getPacket());
    }

    if (ret < 0) {
        ALOGE("Error sending packet for decoding ret:%d", ret);
        return false;
    }
    while (ret >= 0) {
        std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
        ret = avcodec_receive_frame(mVideoCtx, frame->getFrame());
        if (ret == AVERROR(EAGAIN)){
            ret = OK;
            break;
        }else if(ret == AVERROR_EOF){
            frame->setFrameType(E_FRAME_TYPE_VIDEO);
            queueFrame(frame);
            ret = UNKNOWN_ERROR;
            break;
        }else if (ret < 0) {
            ALOGE("Error during decoding %d", ret);
            break;
        }

        std::shared_ptr<VEFrame> videoFrame = std::make_shared<VEFrame>(frame->getFrame()->width,frame->getFrame()->height,AV_PIX_FMT_YUV420P);
        videoFrame->setFrameType(E_FRAME_TYPE_VIDEO);
        ConvertYUV420PWithStrideToContinuous(frame->getFrame()->data[0],frame->getFrame()->linesize[0],
                                             frame->getFrame()->data[1],frame->getFrame()->linesize[1],
                                             frame->getFrame()->data[2],frame->getFrame()->linesize[2],
                                             videoFrame->getFrame()->data[0],videoFrame->getFrame()->data[1],videoFrame->getFrame()->data[2],
                                             frame->getFrame()->width,frame->getFrame()->height);

        videoFrame->setTimestamp(av_rescale_q(frame->getFrame()->pts,mMediaInfo->mVideoTimeBase,{1,AV_TIME_BASE}));
        ALOGD("video frame pts:%" PRId64,videoFrame->getTimestamp());
        queueFrame(videoFrame);
        ret = OK;
    }
    ALOGI("VEVideoDecoder::%s exit",__FUNCTION__ );
    return ret;
}

status_t VEVideoDecoder::onUninit() {
    ALOGI("VEVideoDecoder::%s",__FUNCTION__ );
    if(mVideoCtx){
        avcodec_free_context(&mVideoCtx);
    }
    return false;
}

int VEVideoDecoder::init(std::shared_ptr<VEDemux> demux) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,shared_from_this());
    msg->setObject("demux",demux);
    msg->post();
    return 0;
}

void VEVideoDecoder::start() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart,shared_from_this());
    msg->post();
}

void VEVideoDecoder::stop() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop,shared_from_this());
    msg->post();
}

int VEVideoDecoder::flush() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush,shared_from_this());
    msg->post();
    return 0;
}

int VEVideoDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
    std::unique_lock<std::mutex> lk(mMutex);
    if(mFrameQueue.size() == 0){
        mCond.wait(lk);
    }

    if(mFrameQueue.size() >= FRAME_QUEUE_MAX_SIZE){
        frame = mFrameQueue.front();
        mFrameQueue.pop_front();
        mCond.notify_one();
    }else{
        frame = mFrameQueue.front();
        mFrameQueue.pop_front();
    }
    return 0;
}

int VEVideoDecoder::uninit() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit,shared_from_this());
    msg->post();
    return 0;
}

void VEVideoDecoder::queueFrame(std::shared_ptr<VEFrame> videoFrame) {
    std::unique_lock<std::mutex> lk(mMutex);
    if(mFrameQueue.size() >= FRAME_QUEUE_MAX_SIZE ){
        mCond.wait(lk);
    }

    if(mFrameQueue.size() == 0){
        mFrameQueue.push_back(videoFrame);
        mCond.notify_one();
    }else{
        mFrameQueue.push_back(videoFrame);
    }
}
