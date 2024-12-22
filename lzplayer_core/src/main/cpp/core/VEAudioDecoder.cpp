#include"VEAudioDecoder.h"

#define AUDIO_FRAME_QUEUE_SIZE    50

#define AUDIO_TARGET_OUTPUT_FORMAT        AV_SAMPLE_FMT_S16
#define AUDIO_TARGET_OUTPUT_SAMPLERATE    44100
#define AUDIO_TARGET_OUTPUT_CHANNELS      2

VEAudioDecoder::VEAudioDecoder(){
    mAudioCtx = nullptr;
    mMediaInfo = nullptr;
    mIsStarted = false;
//    fp = fopen("/data/local/tmp/dump_dec.pcm","wb+");
}

VEAudioDecoder::~VEAudioDecoder()
{
//    if(fp){
//        fflush(fp);
//        fclose(fp);
//    }
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
    ALOGI("VEAudioDecoder::readFrame mFrameQueue size:%lu",mFrameQueue.size());
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
            if(onDecode() == OK){
                std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatDecode,shared_from_this());
                msg->post();
            }
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

status_t VEAudioDecoder::onInit(std::shared_ptr<AMessage> msg) {
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

status_t VEAudioDecoder::onFlush() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    avcodec_flush_buffers(mAudioCtx);

    // 清空帧队列
    std::unique_lock<std::mutex> lk(mMutex);
    mFrameQueue.clear();
    mCond.notify_all();

    return false;
}

status_t VEAudioDecoder::onDecode() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    std::shared_ptr<VEPacket> packet;
    mDemux->read(true,packet);
    int ret =0;
    if(packet->getPacketType() == E_PACKET_TYPE_AUDIO){
        // 从解码器中读取音频帧
        ret = avcodec_send_packet(mAudioCtx, packet->getPacket());
        ALOGI("VEAudioDecoder::%s send packet pts:%" PRId64 ", dts:%" PRId64,__FUNCTION__ ,packet->getPacket()->pts, packet->getPacket()->dts);
    }else if(packet->getPacketType() == E_PACKET_TYPE_EOF){
        ret = avcodec_send_packet(mAudioCtx, nullptr);
    }

    if (ret < 0) {
        ALOGE("VEAudioDecoder Error sending packet for decoding", ret);
        return false;
    }
    while (ret >= 0) {
        std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
        ret = avcodec_receive_frame(mAudioCtx, frame->getFrame());
        if (ret == AVERROR(EAGAIN)){
            ret = OK;
            break;
        }else if(ret == AVERROR_EOF) {
            frame->setFrameType(E_FRAME_TYPE_EOF);
            queueFrame(frame);
            ret = UNKNOWN_ERROR;
            break;
        } else if (ret < 0) {
            ALOGE("VEAudioDecoder Error during decoding", ret);
            ret = UNKNOWN_ERROR;
            break;
        }
        ALOGI("###VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64, frame->getFrame()->pts, frame->getFrame()->pkt_dts);

        if(frame->getFrame()->format != (int)AUDIO_TARGET_OUTPUT_FORMAT || frame->getFrame()->sample_rate != AUDIO_TARGET_OUTPUT_SAMPLERATE || frame->getFrame()->ch_layout.nb_channels != AUDIO_TARGET_OUTPUT_CHANNELS){
            if(mSwrCtx == nullptr){

                mSwrCtx = swr_alloc();

                // Set options
                av_opt_set_int(mSwrCtx, "in_channel_layout",  av_get_default_channel_layout(frame->getFrame()->ch_layout.nb_channels), 0);
                av_opt_set_int(mSwrCtx, "in_sample_rate",     frame->getFrame()->sample_rate, 0);
                av_opt_set_sample_fmt(mSwrCtx, "in_sample_fmt", static_cast<AVSampleFormat>(frame->getFrame()->format), 0);

                av_opt_set_int(mSwrCtx, "out_channel_layout", av_get_default_channel_layout(AUDIO_TARGET_OUTPUT_CHANNELS), 0);
                av_opt_set_int(mSwrCtx, "out_sample_rate",    AUDIO_TARGET_OUTPUT_SAMPLERATE, 0);
                av_opt_set_sample_fmt(mSwrCtx, "out_sample_fmt", AUDIO_TARGET_OUTPUT_FORMAT, 0);
                if (swr_init(mSwrCtx) < 0) {
                    ALOGE("VEAudioDecoder Failed to initialize the resampling context");
                    swr_free(&mSwrCtx);
                    return -1;
                }
            }

            uint8_t **out_data = NULL;
            swr_get_out_samples(mSwrCtx,frame->getFrame()->nb_samples);
            int out_nb_samples = swr_get_out_samples(mSwrCtx,frame->getFrame()->nb_samples);
            int out_samples_per_channel;
            int out_buffer_size;

            // 分配输出缓冲区
            av_samples_alloc_array_and_samples(&out_data, &out_buffer_size, AUDIO_TARGET_OUTPUT_CHANNELS,
                                               out_nb_samples, AUDIO_TARGET_OUTPUT_FORMAT, 0);
            memset(out_data[0],0,out_buffer_size);
            // 执行重采样
            out_samples_per_channel = swr_convert(mSwrCtx, out_data, out_nb_samples,
                                                  (const uint8_t **)frame->getFrame()->data, frame->getFrame()->nb_samples);

            if (out_samples_per_channel < 0) {
                ALOGE("VEAudioDecoder swr_convert failed\n");
                return -1;
            }
            std::shared_ptr<VEFrame> audioFrame = std::make_shared<VEFrame>(AUDIO_TARGET_OUTPUT_SAMPLERATE,AUDIO_TARGET_OUTPUT_CHANNELS,out_samples_per_channel,(int)AUDIO_TARGET_OUTPUT_FORMAT);
            ALOGI("VEAudioDecoder out_samples_per_channel:%d  len:%d  linesize:%d out_buffer_size:%d",out_samples_per_channel,out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS * av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT),audioFrame->getFrame()->linesize[0],out_buffer_size);

            memcpy(audioFrame->getFrame()->data[0],out_data[0],out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS * av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT));
            audioFrame->getFrame()->pts = frame->getFrame()->pts;
            audioFrame->getFrame()->pkt_dts = frame->getFrame()->pkt_dts;
            audioFrame->getFrame()->nb_samples = out_samples_per_channel;
            audioFrame->getFrame()->linesize[0] = out_samples_per_channel * AUDIO_TARGET_OUTPUT_CHANNELS * av_get_bytes_per_sample(AUDIO_TARGET_OUTPUT_FORMAT);

            audioFrame->setPts(audioFrame->getFrame()->pts);
            audioFrame->setDts(audioFrame->getFrame()->pkt_dts);
            ALOGI("@@@VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64, audioFrame->getFrame()->pts, audioFrame->getFrame()->pkt_dts);
            av_freep(&out_data[0]);
            av_freep(&out_data);
            audioFrame->setFrameType(E_FRAME_TYPE_AUDIO);
            queueFrame(audioFrame);
        }else{
            frame->setFrameType(E_FRAME_TYPE_AUDIO);
            queueFrame(frame);
        }
        ret = OK;
        // 处理解码后的音频帧
        ALOGI("VEAudioDecoder Audio frame: pts=%s, nb_samples=%d, channels=%d samplerate:%d format:%d\n",
               av_ts2str(frame->getFrame()->pts),
              frame->getFrame()->nb_samples,
              frame->getFrame()->channels,
              frame->getFrame()->sample_rate,
              frame->getFrame()->format);
    }
    ALOGI("VEAudioDecoder::%s exit,ret:%d",__FUNCTION__ ,ret);
    return ret;
}

status_t VEAudioDecoder::onUnInit() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    // 释放资源，包括解码器上下文等
    if (mAudioCtx) {
        avcodec_free_context(&mAudioCtx);
    }

    mMediaInfo = nullptr;
    return false;
}

status_t VEAudioDecoder::onStart() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    mIsStarted = true;
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatDecode,shared_from_this());
    msg->post();
    return false;
}

status_t VEAudioDecoder::onStop() {
    ALOGI("VEAudioDecoder::%s enter",__FUNCTION__ );
    mIsStarted = false;

    // 停止解码器
    avcodec_flush_buffers(mAudioCtx);

    // 清空帧队列
    std::unique_lock<std::mutex> lk(mMutex);
    mFrameQueue.clear();
    mCond.notify_all();

    return false;
}

void VEAudioDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
    std::unique_lock<std::mutex> lk(mMutex);
    if(mFrameQueue.size() >= AUDIO_FRAME_QUEUE_SIZE){
        mCond.wait(lk);
    }

    if(mFrameQueue.size() == 0){
        mFrameQueue.push_back(frame);
        mCond.notify_one();
    }else{
        mFrameQueue.push_back(frame);
    }
}
