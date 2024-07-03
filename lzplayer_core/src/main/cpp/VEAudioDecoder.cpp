#include"VEAudioDecoder.h"

#define AUDIO_FRAME_QUEUE_SIZE    50

VEAudioDecoder::VEAudioDecoder(){
    mAudioCtx = nullptr;
    mMediaInfo = nullptr;
}

VEAudioDecoder::~VEAudioDecoder()
{
    uninit();
}

int VEAudioDecoder::init(std::shared_ptr<VEDemux> demux)
{
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,shared_from_this());
    msg->setObject("demux",demux);
    msg->post();
    return 0;
}

int VEAudioDecoder::flush()
{
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush,shared_from_this());
    msg->post();
    return 0;
}

int VEAudioDecoder::readFrame(std::shared_ptr<VEFrame> &frame)
{
    std::unique_lock<std::mutex> lk(mMutex);
    if(mFrameQueue.size() == 0){
        mCond.wait(lk);
    }else if(mFrameQueue.size() == AUDIO_FRAME_QUEUE_SIZE){
        frame = mFrameQueue.front();
        mFrameQueue.pop_front();
        mCond.notify_one();
    }else{
        frame = mFrameQueue.front();
        mFrameQueue.pop_front();
    }
    return 0;
}

int VEAudioDecoder::uninit()
{
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit,shared_from_this());
    msg->post();
    return 0;
}

void VEAudioDecoder::start() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart,shared_from_this());
    msg->post();
}

void VEAudioDecoder::stop() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop,shared_from_this());
    msg->post();
}

void VEAudioDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatInit:{
            onInit(msg);
            break;
        }
        case kWhatStop:{
            break;
        }
        case kWhatStart:{
            break;
        }
        case kWhatRead:{
            onDecode();
            break;
        }
        case kWhatFlush:{
            onFlush();
            break;
        }default:{
            break;
        }
    }
}

bool VEAudioDecoder::onInit(std::shared_ptr<AMessage> msg) {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    std::shared_ptr<void> tmp;
    msg->findObject("demux",&tmp);

    std::shared_ptr<VEDemux> demux = std::static_pointer_cast<VEDemux>(tmp);
    std::shared_ptr<VEMediaInfo> info = demux->getFileInfo();
    if (info == nullptr) {
        return -1;
    }

    // 根据音频编码格式初始化解码器
    const AVCodec *codec = avcodec_find_decoder(info->mAudioCodecParams->codec_id);
    if (codec == nullptr) {
        return -1;
    }

    // 创建解码器上下文
    mAudioCtx = avcodec_alloc_context3(codec);
    if (mAudioCtx == nullptr) {
        return -1;
    }

    // 设置解码器参数
    avcodec_parameters_to_context(mAudioCtx, info->mAudioCodecParams);

    // 打开解码器
    if (avcodec_open2(mAudioCtx, codec, nullptr) != 0) {
        return -1;
    }
    return false;
}

bool VEAudioDecoder::onFlush() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    avcodec_flush_buffers(mAudioCtx);
    return false;
}

bool VEAudioDecoder::onDecode() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    std::shared_ptr<VEPacket> packet;
    mDemux->read(true,packet);
    // 从解码器中读取音频帧
    int ret = avcodec_send_packet(mAudioCtx, packet->getPacket());
    if (ret < 0) {
        ALOGE("Error sending packet for decoding", ret);
        return false;
    }
    while (ret >= 0) {
        std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
        ret = avcodec_receive_frame(mAudioCtx, frame->getFrame());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            ALOGE("Error during decoding", ret);
            break;
        }
        std::unique_lock<std::mutex> lk(mMutex);
        if(mFrameQueue.size() >= AUDIO_FRAME_QUEUE_SIZE){
            mCond.wait(lk);
        }else if(mFrameQueue.size() == 0){
            mFrameQueue.push_back(frame);
            mCond.notify_one();
        }else{
            mFrameQueue.push_back(frame);
        }
        // 处理解码后的音频帧
        ALOGI("Audio frame: pts=%s, nb_samples=%d, channels=%d\n",
               av_ts2str(frame->getFrame()->pts),
              frame->getFrame()->nb_samples,
              frame->getFrame()->channels);
    }
    return false;
}

bool VEAudioDecoder::onUnInit() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    // 释放资源，包括解码器上下文等
    if (mAudioCtx) {
        avcodec_free_context(&mAudioCtx);
    }

    mMediaInfo = nullptr;
    return false;
}
