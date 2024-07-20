#include "VEDemux.h"
#include "VEPacket.h"
extern "C"{
    #include"libavcodec/avcodec.h"
    #include"libavformat/avformat.h"
    #include"libavutil/dict.h"
}

#define AUDIO_QUEUE_SIZE    50
#define VIDEO_QUEUE_SIZE    10

VEDemux::VEDemux()
{
    mAudioCodecParams = nullptr;
    mVideoCodecParams = nullptr;
    mFormatContext = nullptr;
}

VEDemux::~VEDemux()
{

}

status_t VEDemux::open(std::string file)
{
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatOpen,shared_from_this());
    msg->setString("filePath",file);

    std::shared_ptr<AMessage> respone;
    msg->postAndAwaitResponse(&respone);
    int32_t ret;
    respone->findInt32("ret",&ret);
    return ret;
}

status_t VEDemux::read(bool isAudio, std::shared_ptr<VEPacket> &packet){

    if(isAudio){
        std::unique_lock<std::mutex> lock(mMutexAudio);
        ALOGD("mAudioPacketQueue size:%zu",mAudioPacketQueue.size());
        if(mAudioPacketQueue.size() == 0){
            mCondAudio.wait(lock);
        }

        if(mAudioPacketQueue.size() >= AUDIO_QUEUE_SIZE){
            packet = mAudioPacketQueue.front();
            mAudioPacketQueue.pop_front();
            mCondAudio.notify_one();
        }else{
            packet = mAudioPacketQueue.front();
            mAudioPacketQueue.pop_front();
        }
    }else{
        std::unique_lock<std::mutex> lock(mMutexVideo);
        if(mVideoPacketQueue.size() == 0){
            mCondVideo.wait(lock);
        }

        if(mVideoPacketQueue.size() >= VIDEO_QUEUE_SIZE){
            packet = mVideoPacketQueue.front();
            mVideoPacketQueue.pop_front();
            mCondVideo.notify_one();
        }else{
            packet = mVideoPacketQueue.front();
            mVideoPacketQueue.pop_front();
        }

    }
    return OK;
}

status_t VEDemux::seek(uint64_t pos)
{
    if (!mFormatContext) {
        fprintf(stderr, "Error: File not opened.\n");
        return -1;
    }

    int64_t seekTarget = av_rescale_q(pos, AV_TIME_BASE_Q, mFormatContext->streams[0]->time_base);
    
    if (av_seek_frame(mFormatContext, -1, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        fprintf(stderr, "Error: Couldn't seek.\n");
        return -1;
    }

    return 0;
}

status_t VEDemux::close()
{
    if (mFormatContext) {
        avformat_close_input(&mFormatContext);
        mFormatContext = nullptr;
    }

    if(mAudioCodecParams){
        avcodec_parameters_free(&mAudioCodecParams);
        mAudioCodecParams = nullptr;
    }
    if(mVideoCodecParams){
        avcodec_parameters_free(&mVideoCodecParams);
        mVideoCodecParams = nullptr;
    }
    return 0;
}

std::shared_ptr<VEMediaInfo> VEDemux::getFileInfo()
{
    std::shared_ptr<VEMediaInfo> tmp = std::make_shared<VEMediaInfo>();

    tmp->channels = mChannel;
    tmp->duration = mDuration;
    tmp->fps = mFps;
    tmp->width = mWidth;
    tmp->height = mHeight;
    tmp->sampleRate = mSampleRate;
    tmp->sampleFormat = mSampleFormat;
    tmp->mAudioCodecParams = mAudioCodecParams;
    tmp->mVideoCodecParams = mVideoCodecParams;
    tmp->audio_stream_index = mAudio_index;
    tmp->video_stream_index = mVideo_index;
    tmp->mAStartTime = mAStartTime;
    tmp->mAudioTimeBase = mAudioTimeBase;
    tmp->mVideoTimeBase = mVideoTimeBase;
    tmp->mVStartTime = mVStartTime;
    return tmp;
}

void VEDemux::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatOpen:{
            std::string path;
            msg->findString("filePath",path);
            std::shared_ptr<AReplyToken> replyToken;
            msg->senderAwaitsResponse(replyToken);
            status_t ret = onOpen(path);

            std::shared_ptr<AMessage> replyMsg = std::make_shared<AMessage>();
            replyMsg->setInt32("ret",ret);
            replyMsg->postReply(replyToken);
            break;
        }
        case kWhatStart:{
            mIsStart = true;
            onStart();
            break;
        }
        case kWhatStop:{
            mIsStart = false;
            break;
        }
        case kWhatPause:{
            mIsPause = true;
            break;
        }
        case kWhatResume:{
            mIsPause = false;
            break;
        }
        case kWhatRead:{
            if(mIsPause || !mIsStart){
                break;
            }
            onRead();
            break;
        }
    }
}

void VEDemux::start() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart,shared_from_this());
    msg->post();
}

void VEDemux::stop() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop,shared_from_this());
    msg->post();
}

void VEDemux::pause() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause,shared_from_this());
    msg->post();
}

void VEDemux::resume() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatResume,shared_from_this());
    msg->post();
}

status_t VEDemux::onOpen(std::string path) {
    ALOGI("VEDemux::%s",__FUNCTION__ );
    ///打开文件
    if(path.empty()){
        printf("## %s  %d open file failed!!!",__FUNCTION__,__LINE__);
        return -1;
    }

    mFilePath = path;

    if (avformat_open_input(&mFormatContext, mFilePath.c_str(), nullptr, nullptr) != 0) {
        fprintf(stderr, "Error: Couldn't open input file.\n");
        return -1;
    }

    // 获取流信息
    if (avformat_find_stream_info(mFormatContext, nullptr) < 0) {
        fprintf(stderr, "Error: Couldn't find stream information.\n");
        avformat_close_input(&mFormatContext);
        return -1;
    }
    mDuration = mFormatContext->duration/1000;
    ///获取文件信息
    for (unsigned int i = 0; i < mFormatContext->nb_streams; i++) {
        AVStream* stream = mFormatContext->streams[i];

        if(stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            mAudio_index = i;
            mAudioTimeBase = stream->time_base;
            mAStartTime = stream->start_time;
            mAudioCodecParams = avcodec_parameters_alloc();
            avcodec_parameters_copy(mAudioCodecParams,stream->codecpar);
            mChannel = mAudioCodecParams->channels;
            mSampleFormat = mAudioCodecParams->format;
            mSampleRate = mAudioCodecParams->sample_rate;
        }else if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            mVideo_index = i;
            mVideoTimeBase = stream->time_base;
            mVStartTime = stream->start_time;
            mVideoCodecParams = avcodec_parameters_alloc();
            avcodec_parameters_copy(mVideoCodecParams,stream->codecpar);
            mWidth = mVideoCodecParams->width;
            mHeight = mVideoCodecParams->height;
            mFps = stream->r_frame_rate.num/stream->r_frame_rate.den;
        }
    }
    return 0;
}

status_t VEDemux::onStart() {
    ALOGI("VEDemux::%s",__FUNCTION__ );
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,shared_from_this());
    msg->post();
    return 0;
}

status_t VEDemux::onRead() {
    ALOGI("VEDemux::%s",__FUNCTION__ );
    std::shared_ptr<VEPacket> packet = std::make_shared<VEPacket>();
    if(!packet){
        ALOGD("Could not allocate AVPacket");
        return NO_ERROR;
    }

    if(av_read_frame(mFormatContext, packet->getPacket()) <0){
        return -1;
    }
    if(packet->getPacket()->stream_index == mAudio_index){
        ALOGD("%s packet pts:%" PRId64,packet->getPacket()->stream_index == mAudio_index ? "audio":"video" ,packet->getPacket()->pts);

        std::unique_lock<std::mutex> lk(mMutexAudio);
        if(mAudioPacketQueue.size() >= AUDIO_QUEUE_SIZE){
            mCondAudio.wait(lk);
        }
        if(mAudioPacketQueue.size() == 0){
            mAudioPacketQueue.push_back(packet);
            mCondAudio.notify_one();
        }else{
            mAudioPacketQueue.push_back(packet);
        }
        ALOGD("mAudioPacketQueue size:%zu",mAudioPacketQueue.size());
    }else if (packet->getPacket()->stream_index == mVideo_index)
    {
        std::unique_lock<std::mutex> lk(mMutexVideo);
        if(mVideoPacketQueue.size() >= VIDEO_QUEUE_SIZE){
            mCondVideo.wait(lk);
        }
        if(mVideoPacketQueue.size() == 0){
            mVideoPacketQueue.push_back(packet);
            mCondVideo.notify_one();
        }else{
            mVideoPacketQueue.push_back(packet);
        }
    }else{
        ALOGD("may be not use");
    }

    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,shared_from_this());
    msg->post();
    return 0;
}
