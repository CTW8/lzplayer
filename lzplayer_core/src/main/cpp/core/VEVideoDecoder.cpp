#include"VEVideoDecoder.h"
#define FRAME_QUEUE_MAX_SIZE  3
#include "libyuv.h"
#include "Errors.h"

void ConvertYUV420PWithStrideToContinuous(const uint8_t* src_y, int src_stride_y,
                                          const uint8_t* src_u, int src_stride_u,
                                          const uint8_t* src_v, int src_stride_v,
                                          uint8_t* dst_y, uint8_t* dst_u, uint8_t* dst_v,
                                          int width, int height) {
    ALOGI("ConvertYUV420PWithStrideToContinuous enter");
    // 复制 Y 平面
    libyuv::CopyPlane(src_y, src_stride_y, dst_y, width, width, height);
    // 复制 U 平面
    libyuv::CopyPlane(src_u, src_stride_u, dst_u, width / 2, width / 2, height / 2);
    // 复制 V 平面
    libyuv::CopyPlane(src_v, src_stride_v, dst_v, width / 2, width / 2, height / 2);
}

VEVideoDecoder::VEVideoDecoder()
{
    ALOGI("VEVideoDecoder::VEVideoDecoder enter");
    mVideoCtx = nullptr;
    mMediaInfo = nullptr;
    mIsStoped = false;
//    fp = fopen("/data/local/tmp/dump_dec.yuv","wb+");
}

VEVideoDecoder::~VEVideoDecoder()
{
    ALOGI("VEVideoDecoder::~VEVideoDecoder enter");
//    if(fp){
//        fflush(fp);
//        fclose(fp);
//    }
}

void VEVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VEVideoDecoder::onMessageReceived enter");
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
    ALOGI("VEVideoDecoder::onInit enter");
    std::shared_ptr<void> tmp;
    msg->findObject("demux",&tmp);

    mDemux = std::static_pointer_cast<VEDemux>(tmp);

    mMediaInfo = mDemux->getFileInfo();

    const AVCodec *video_codec = avcodec_find_decoder(mMediaInfo->mVideoCodecParams->codec_id);
    if (!video_codec) {
        ALOGE("VEVideoDecoder::onInit Could not find video codec");
        return UNKNOWN_ERROR;
    }
    mVideoCtx = avcodec_alloc_context3(video_codec);
    if (!mVideoCtx) {
        ALOGE("VEVideoDecoder::onInit Could not allocate video codec context\n");
        return UNKNOWN_ERROR;
    }
    if (avcodec_parameters_to_context(mVideoCtx, mMediaInfo->mVideoCodecParams) < 0) {
        ALOGE("VEVideoDecoder::onInit Could not copy codec parameters to codec context");
        avcodec_free_context(&mVideoCtx);
        return UNKNOWN_ERROR;
    }
    if (avcodec_open2(mVideoCtx, video_codec, NULL) < 0) {
        fprintf(stderr, "VEVideoDecoder::onInit Could not open video codec\n");
        avcodec_free_context(&mVideoCtx);
        return UNKNOWN_ERROR;
    }

    return OK;
}

status_t VEVideoDecoder::onStart() {
    ALOGI("VEVideoDecoder::onStart enter");
    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatDecode,shared_from_this());
    readMsg->post();
    return OK;
}

status_t VEVideoDecoder::onStop() {
    ALOGI("VEVideoDecoder::onStop enter");

    // 停止解码器
    avcodec_flush_buffers(mVideoCtx);

    // 清空帧队列
    std::unique_lock<std::mutex> lk(mMutex);
    mFrameQueue.clear();
    mCond.notify_all();

    // 停止解码线程
    mIsStoped = true;

    return OK;
}

status_t VEVideoDecoder::onFlush() {
    ALOGI("VEVideoDecoder::onFlush enter");

    // 清空解码器缓冲区
    avcodec_flush_buffers(mVideoCtx);

    // 清空帧队列
    std::unique_lock<std::mutex> lk(mMutex);
    mFrameQueue.clear();
    mCond.notify_all();

    return OK;
}

status_t VEVideoDecoder::onDecode() {
    ALOGI("VEVideoDecoder::onDecode enter");
    if (mIsStoped) {
        ALOGI("VEVideoDecoder::onDecode stop requested, exiting");
        return UNKNOWN_ERROR;
    }

    std::shared_ptr<VEPacket> packet;
    mDemux->read(false,packet);
    int ret = 0;
    if(packet->getPacketType() == E_PACKET_TYPE_EOF){
        ret = avcodec_send_packet(mVideoCtx, nullptr);
    }else{
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
        if (ret == AVERROR(EAGAIN)){
            ret = OK;
            break;
        }else if(ret == AVERROR_EOF){
            frame->setFrameType(E_FRAME_TYPE_EOF);
            queueFrame(frame);
            ret = UNKNOWN_ERROR;
            break;
        }else if (ret < 0) {
            ALOGE("VEVideoDecoder::onDecode Error during decoding %d", ret);
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
        videoFrame->setDts(av_rescale_q(frame->getFrame()->pkt_dts,mMediaInfo->mVideoTimeBase,{1,AV_TIME_BASE}));
        ALOGD("VEVideoDecoder::onDecode video frame pts:%" PRId64,videoFrame->getTimestamp());
        queueFrame(videoFrame);
        ret = OK;
    }
    ALOGI("VEVideoDecoder::onDecode exit");
    return ret;
}

status_t VEVideoDecoder::onUninit() {
    ALOGI("VEVideoDecoder::onUninit enter");
    if(mVideoCtx){
        avcodec_free_context(&mVideoCtx);
    }
    return false;
}

int VEVideoDecoder::init(std::shared_ptr<VEDemux> demux) {
    ALOGI("VEVideoDecoder::init enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,shared_from_this());
    msg->setObject("demux",demux);
    msg->post();
    return 0;
}

void VEVideoDecoder::start() {
    ALOGI("VEVideoDecoder::start enter");
    mIsStoped = false;
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart,shared_from_this());
    msg->post();
}

void VEVideoDecoder::stop() {
    ALOGI("VEVideoDecoder::stop enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop,shared_from_this());
    msg->post();
}

int VEVideoDecoder::flush() {
    ALOGI("VEVideoDecoder::flush enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush,shared_from_this());
    msg->post();
    return 0;
}

int VEVideoDecoder::readFrame(std::shared_ptr<VEFrame> &frame) {
    ALOGI("VEVideoDecoder::readFrame enter");
    ALOGD("VEVideoDecoder::readFrame %s enter",__FUNCTION__ );
    std::unique_lock<std::mutex> lk(mMutex);
    ALOGI("VEVideoDecoder::readFrame %s mFrameQueue size:%d",__FUNCTION__ ,mFrameQueue.size());
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
    ALOGI("VEVideoDecoder::uninit enter");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit,shared_from_this());
    msg->post();
    return 0;
}

void VEVideoDecoder::queueFrame(std::shared_ptr<VEFrame> videoFrame) {
    ALOGI("VEVideoDecoder::queueFrame enter");
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
