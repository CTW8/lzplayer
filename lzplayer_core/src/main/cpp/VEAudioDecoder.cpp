#include"VEAudioDecoder.h"

#define AUDIO_FRAME_QUEUE_SIZE    50

#define AUDIO_TARGET_OUTPUT_FORMAT        AV_SAMPLE_FMT_S16
#define AUDIO_TARGET_OUTPUT_SAMPLERATE    44100
#define AUDIO_TARGET_OUTPUT_CHANNELS      2

VEAudioDecoder::VEAudioDecoder(){
    mAudioCtx = nullptr;
    mMediaInfo = nullptr;
    mIsStarted = false;
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
    ALOGI("VEAudioDecoder::readFrame mFrameQueue size:%d",mFrameQueue.size());
    if(mFrameQueue.size() == 0){
        mCond.wait(lk);
    }

    if(mFrameQueue.size() == AUDIO_FRAME_QUEUE_SIZE){
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
            onStop();
            break;
        }
        case kWhatStart:{
            onStart();
            break;
        }
        case kWhatDecode:{
            if(!mIsStarted){
                break;
            }
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

    mDemux = std::static_pointer_cast<VEDemux>(tmp);
    std::shared_ptr<VEMediaInfo> info = mDemux->getFileInfo();
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
        if(frame->getFrame()->format != (int)AUDIO_TARGET_OUTPUT_FORMAT || frame->getFrame()->sample_rate != AUDIO_TARGET_OUTPUT_SAMPLERATE || frame->getFrame()->ch_layout.nb_channels != AUDIO_TARGET_OUTPUT_CHANNELS){
            if(mSwrCtx == nullptr){

                AVChannelLayout out_ch_layout;
                av_channel_layout_default(&out_ch_layout, AUDIO_TARGET_OUTPUT_CHANNELS);

                // 使用 swr_alloc_set_opts2 创建和配置 SwrContext
                ret = swr_alloc_set_opts2(&mSwrCtx,
                                          &out_ch_layout, AUDIO_TARGET_OUTPUT_FORMAT, AUDIO_TARGET_OUTPUT_SAMPLERATE,
                                          &frame->getFrame()->ch_layout,
                                          static_cast<AVSampleFormat>(frame->getFrame()->format), frame->getFrame()->sample_rate,
                                          0, NULL);
                if (ret < 0) {
                    ALOGE("Could not allocate SwrContext\n");
                    return ret;
                }
                if (swr_init(mSwrCtx) < 0) {
                    fprintf(stderr, "Failed to initialize the resampling context\n");
                    swr_free(&mSwrCtx);
                    return -1;
                }

                av_channel_layout_uninit(&out_ch_layout);
            }

            uint8_t **out_data = NULL;
            int out_nb_samples = av_rescale_rnd(frame->getFrame()->nb_samples, AUDIO_TARGET_OUTPUT_SAMPLERATE, frame->getFrame()->sample_rate, AV_ROUND_UP);
            int out_samples_per_channel;
            int out_buffer_size;

            // 分配输出缓冲区
            av_samples_alloc_array_and_samples(&out_data, &out_buffer_size, AUDIO_TARGET_OUTPUT_CHANNELS,
                                               out_nb_samples, AUDIO_TARGET_OUTPUT_FORMAT, 0);

            // 执行重采样
            out_samples_per_channel = swr_convert(mSwrCtx, out_data, out_nb_samples,
                                                  (const uint8_t **)frame->getFrame()->data, frame->getFrame()->nb_samples);

            if (out_samples_per_channel < 0) {
                ALOGE("swr_convert failed\n");
                return -1;
            }
            ALOGI("out_samples_per_channel:%d",out_samples_per_channel);
            std::shared_ptr<VEFrame> audioFrame = std::make_shared<VEFrame>(AUDIO_TARGET_OUTPUT_SAMPLERATE,AUDIO_TARGET_OUTPUT_CHANNELS,out_samples_per_channel,(int)AUDIO_TARGET_OUTPUT_FORMAT);
            memcpy(audioFrame->getFrame()->data,out_data,out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS * av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT));
            audioFrame->getFrame()->pts = frame->getFrame()->pts;

            std::unique_lock<std::mutex> lk(mMutex);
            if(mFrameQueue.size() >= AUDIO_FRAME_QUEUE_SIZE){
                mCond.wait(lk);
            }else if(mFrameQueue.size() == 0){
                mFrameQueue.push_back(audioFrame);
                mCond.notify_one();
            }else{
                mFrameQueue.push_back(audioFrame);
            }
        }else{
            std::unique_lock<std::mutex> lk(mMutex);
            if(mFrameQueue.size() >= AUDIO_FRAME_QUEUE_SIZE){
                mCond.wait(lk);
            }else if(mFrameQueue.size() == 0){
                mFrameQueue.push_back(frame);
                mCond.notify_one();
            }else{
                mFrameQueue.push_back(frame);
            }
        }
        // 处理解码后的音频帧
        ALOGI("Audio frame: pts=%s, nb_samples=%d, channels=%d samplerate:%d format:%d\n",
               av_ts2str(frame->getFrame()->pts),
              frame->getFrame()->nb_samples,
              frame->getFrame()->channels,
              frame->getFrame()->sample_rate,
              frame->getFrame()->format);
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

bool VEAudioDecoder::onStart() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    mIsStarted = true;
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatDecode,shared_from_this());
    msg->post();
    return false;
}

bool VEAudioDecoder::onStop() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    mIsStarted = false;
    return false;
}
