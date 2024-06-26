#include"VEVideoDecoder.h"

VEVideoDecoder::VEVideoDecoder()
{
    mVideoCtx = nullptr;
    mMediaInfo = nullptr;
}

VEVideoDecoder::~VEVideoDecoder()
{
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
        case kWhatRead:{
            onDecode();
            break;
        }
        case kWhatUninit:{
            onUninit();
            break;
        }
    }
}

bool VEVideoDecoder::onInit(std::shared_ptr<AMessage> msg) {
    std::shared_ptr<void> tmp;
    msg->findObject("demux",&tmp);

    mDemux = std::static_pointer_cast<VEDemux>(tmp);

    std::shared_ptr<VEMediaInfo> info = mDemux->getFileInfo();

    const AVCodec *video_codec = avcodec_find_decoder(info->mVideoCodecParams->codec_id);
    if (!video_codec) {
        ALOGE("Could not find video codec");
        return false;
    }
    mVideoCtx = avcodec_alloc_context3(video_codec);
    if (!mVideoCtx) {
        ALOGE("Could not allocate video codec context\n");
        return false;
    }
    if (avcodec_parameters_to_context(mVideoCtx, info->mVideoCodecParams) < 0) {
        ALOGE("Could not copy codec parameters to codec context");
        avcodec_free_context(&mVideoCtx);
        return false;
    }
    if (avcodec_open2(mVideoCtx, video_codec, NULL) < 0) {
        fprintf(stderr, "Could not open video codec\n");
        avcodec_free_context(&mVideoCtx);
        return false;
    }

    return false;
}

bool VEVideoDecoder::onStart() {
    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatRead,shared_from_this());
    readMsg->post();
    return false;
}

bool VEVideoDecoder::onStop() {
    return false;
}

bool VEVideoDecoder::onFlush() {
    return false;
}

bool VEVideoDecoder::onDecode() {
    std::shared_ptr<VEPacket> packet;
    mDemux->read(false,packet);
    // 从解码器中读取音频帧
    int ret = avcodec_send_packet(mVideoCtx, packet->getPacket());
    if (ret < 0) {
        ALOGE("Error sending packet for decoding", ret);
        return false;
    }
    while (ret >= 0) {
        std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
        ret = avcodec_receive_frame(mVideoCtx, frame->getFrame());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            ALOGE("Error during decoding", ret);
            break;
        }
        std::unique_lock<std::mutex> lk(mMutex);
        if(mFrameQueue.size() >= 10){
            mCond.wait(lk);
        }else if(mFrameQueue.size() == 0){
            mFrameQueue.push_back(frame);
            mCond.notify_one();
        }else{
            mFrameQueue.push_back(frame);
        }
    }

    std::shared_ptr<AMessage> readMsg = std::make_shared<AMessage>(kWhatRead,shared_from_this());
    readMsg->post();
    return false;
}

bool VEVideoDecoder::onUninit() {
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
    }else if(mFrameQueue.size() == 10){
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
