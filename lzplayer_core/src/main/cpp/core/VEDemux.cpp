#include "VEDemux.h"
#include "VEPacket.h"
#include "VEError.h"

extern "C"{
    #include"libavcodec/avcodec.h"
    #include"libavformat/avformat.h"
    #include"libavutil/dict.h"
}

#define AUDIO_QUEUE_SIZE    100
#define VIDEO_QUEUE_SIZE    100

VEDemux::VEDemux(){
    mAudioCodecParams = nullptr;
    mVideoCodecParams = nullptr;
    mFormatContext = nullptr;
    mAudioStartPts = -1;
    mVideoStartPts = -1;
}

VEDemux::~VEDemux() {
    close();
}

VEResult VEDemux::open(std::string file)
{
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatOpen,shared_from_this());
    msg->setString("filePath",file);

    std::shared_ptr<AMessage> respone;
    msg->postAndAwaitResponse(&respone);
    int32_t ret;
    respone->findInt32("ret",&ret);
    return ret;
}

VEResult VEDemux::read(bool isAudio, std::shared_ptr<VEPacket> &packet){
    ALOGD("VEDemux::read audio queue size: %d, video queue size: %d", mAudioPacketQueue->getDataSize(), mVideoPacketQueue->getDataSize());
    if(isAudio){
        ALOGD("VEDemux::read mAudioPacketQueue size:%d",mAudioPacketQueue->getDataSize());
        if(mAudioPacketQueue->getDataSize() == 0){
            ALOGD("VEDemux::read audio queue wait!!");
            return VE_NOT_ENOUGH_DATA;
        }

        packet = mAudioPacketQueue->get();
    }else{
        ALOGD("VEDemux::read mVideoPacketQueue size:%d",mVideoPacketQueue->getDataSize());
        if(mVideoPacketQueue->getDataSize() == 0){
            ALOGD("VEDemux::read video queue wait!!");
            return VE_NOT_ENOUGH_DATA;
        }

        packet = mVideoPacketQueue->get();
    }
    return VE_OK;
}

VEResult VEDemux::close()
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
            VEResult ret = onOpen(path);

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
        case kWhatSeek:{
            double pos = 0;
            msg->findDouble("posMs",&pos);
            if(onSeek(pos) == VE_OK){
                ALOGI("VEDemux::onMessageReceived Seek done.");
            }
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
            if(onRead() == 0){
                std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,shared_from_this());
                msg->post();
            }
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

VEResult VEDemux::seek(double posMs)
{
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek,shared_from_this());
    msg->setDouble("posMs",posMs);
    msg->post();
    return 0;
}

VEResult VEDemux::onOpen(std::string path) {
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

    mAudioPacketQueue = std::make_shared<VEPacketQueue>(AUDIO_QUEUE_SIZE);
    mVideoPacketQueue = std::make_shared<VEPacketQueue>(VIDEO_QUEUE_SIZE);
    return 0;
}

VEResult VEDemux::onStart() {
    ALOGI("VEDemux::%s",__FUNCTION__ );
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRead,shared_from_this());
    msg->post();
    return 0;
}
VEResult VEDemux::onRead() {
    ALOGI("VEDemux::%s",__FUNCTION__ );

    if (mAudioPacketQueue->getDataSize() >= AUDIO_QUEUE_SIZE) {
        ALOGD("VEDemux::onRead Audio queue is full, stopping read.");
        mIsStart = false;
        return VE_NO_MEMORY;
    }

    if (mVideoPacketQueue->getDataSize() >= VIDEO_QUEUE_SIZE) {
        ALOGD("VEDemux::onRead Video queue is full, stopping read.");
        mIsStart = false;
        return VE_NO_MEMORY;
    }

    std::shared_ptr<VEPacket> packet = std::make_shared<VEPacket>();
    if(!packet){
        ALOGD("VEDemux::onRead Could not allocate AVPacket");
        return NO_ERROR;
    }

    int ret = av_read_frame(mFormatContext, packet->getPacket());
    if (ret == AVERROR_EOF) {
        // 已经到达文件末尾
        ALOGI("VEDemux::onRead End of Stream (EOS) reached.");
        packet->setPacketType(E_PACKET_TYPE_EOF);
        putPacket(packet,true);
        std::shared_ptr<VEPacket> videoPacket = std::make_shared<VEPacket>();
        videoPacket->setPacketType(E_PACKET_TYPE_EOF);
        putPacket(videoPacket,false);
        return -1;
    } else if (ret < 0) {
        // 处理其他错误
        ALOGI("VEDemux::onRead Error occurred: %s", av_err2str(ret));
        return -1;
    }

    int64_t pts = av_rescale_q(packet->getPacket()->pts, mFormatContext->streams[packet->getPacket()->stream_index]->time_base, AV_TIME_BASE_Q);
    int64_t dts = av_rescale_q(packet->getPacket()->dts, mFormatContext->streams[packet->getPacket()->stream_index]->time_base, AV_TIME_BASE_Q);

    if(packet->getPacket()->stream_index == mAudio_index){
        packet->setPacketType(E_PACKET_TYPE_AUDIO);
        if(mAudioStartPts == -1){
            mAudioStartPts = pts;
        }

        packet->setPts(pts);
        packet->setDts(dts);
        packet->getPacket()->pts = packet->getPts();
        packet->getPacket()->dts = packet->getDts();
        ALOGD("VEDemux::onRead Audio packet pts (original): %" PRId64 ", converted: %" PRId64 " | dts (original): %" PRId64 ", converted: %" PRId64, packet->getPacket()->pts, packet->getPts(), packet->getPacket()->dts, packet->getDts());
        putPacket(packet,true);
    }else if (packet->getPacket()->stream_index == mVideo_index){
        packet->setPacketType(E_PACKET_TYPE_VIDEO);
        if(mVideoStartPts == -1){
            mVideoStartPts = pts;
        }

        packet->setPts(pts - mVideoStartPts);
        packet->setDts(dts);
        packet->getPacket()->pts = packet->getPts();
        packet->getPacket()->dts = packet->getDts();
        ALOGD("VEDemux::onRead Video packet pts (original): %" PRId64 ", converted: %" PRId64 " | dts (original): %" PRId64 ", converted: %" PRId64, packet->getPacket()->pts, packet->getPts(), packet->getPacket()->dts, packet->getDts());
        putPacket(packet,false);
    }else{
        ALOGD("VEDemux::onRead may be not use");
    }
    return 0;
}

VEResult VEDemux::onSeek(double posMs) {
    if (!mFormatContext) {
        ALOGE("VEDemux::onSeek Error: File not opened.\n");
        return VE_INVALID_PARAMS;
    }

    ALOGD("VEDemux::onSeek posMs:%f", posMs);

    // 将毫秒转换为目标时间戳
    int64_t targetPts = static_cast<int64_t>(posMs * 1000);

    // 目标流的时间基
    AVRational timeBase = mFormatContext->streams[mVideo_index]->time_base;

    // 目标时间戳转换到流时间基
    int64_t seekTarget = av_rescale_q(targetPts, AV_TIME_BASE_Q, timeBase);

    // 使用 avformat_seek_file 执行 Seek
    int ret = avformat_seek_file(mFormatContext, mVideo_index, INT64_MIN, seekTarget, INT64_MAX, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        ALOGE("VEDemux::onSeek Error: Couldn't seek using avformat_seek_file.\n");
        return -1;
    }

    mAudioPacketQueue->clear();
    mVideoPacketQueue->clear();

    ALOGD("VEDemux::onSeek Successful to posMs: %f", posMs);
    return VE_OK;
}

void VEDemux::putPacket(std::shared_ptr<VEPacket> packet, bool isAudio) {
    if(!isAudio){
        if(!mVideoPacketQueue->put(packet)){
            ALOGD("VEDemux::putPacket Video queue is full, stopping read.");
            mIsStart = false;
        } else {
            ALOGD("VEDemux::putPacket Video queue mNeedVideoMore:%d",mNeedVideoMore);
            std::lock_guard<std::mutex> lk(mMutexVideo);

            if(mNeedVideoMore) {
                mNeedVideoMore = false;
                if (mVideoNotify) {
                    ALOGD("VEDemux::putPacket Video queue post notify");
                    mVideoNotify->post();
                }
            }
        }
    }else{
        if(!mAudioPacketQueue->put(packet)){
            ALOGD("VEDemux::putPacket Audio queue is full, stopping read.");
            mIsStart = false;
        } else {
            ALOGD("VEDemux::putPacket Audio queue mNeedAudioMore:%d",mNeedAudioMore);
            std::lock_guard<std::mutex> lk(mMutexAudio);
            if(mNeedAudioMore) {
                mNeedAudioMore = false;
                if (mAudioNotify) {
                    ALOGD("VEDemux::putPacket Video queue post notify");
                    mAudioNotify->post();
                }
            }
        }
    }
}

void VEDemux::needMorePacket(std::shared_ptr<AMessage> msg, int type) {
    if(type == 1){
        mAudioNotify = msg;
        mNeedAudioMore = true;
        ALOGI("VEDemux::needMorePacket - Need more packets for type audio.");
    }else{
        mVideoNotify = msg;
        mNeedVideoMore = true;
        ALOGI("VEDemux::needMorePacket - Need more packets for type: video.");
    }

    if(mIsStart == false){
        mIsStart = true;
        ALOGI("VEDemux::needMorePacket - Starting to read packets.");
        std::make_shared<AMessage>(kWhatRead,shared_from_this())->post();
    }
}
