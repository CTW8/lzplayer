#include "VEDemux.h"
#include "VEPacket.h"
extern "C"{
    #include"libavcodec/avcodec.h"
    #include"libavformat/avformat.h"
    #include"libavutil/dict.h"
}

VEDemux::VEDemux()
{
    mAudioCodecParams = nullptr;
    mVideoCodecParams = nullptr;
    mFormatContext = nullptr;
}

VEDemux::~VEDemux()
{

}

int VEDemux::open(std::string file)
{
    ///打开文件
    if(file.empty()){
        printf("## %s  %d open file failed!!!",__FUNCTION__,__LINE__);
        return -1;
    }

    mFilePath = file;

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

int32_t VEDemux::read(VEPacket *packet){
    if(av_read_frame(mFormatContext, packet->frame) <0){
        return -1;
    }
    if(packet->frame->stream_index == mAudio_index){
        packet->type = 1;
    }else if (packet->frame->stream_index = mVideo_index)
    {
        packet->type = 0;
    }
    
    return 0;
}

int VEDemux::seek(uint64_t pos)
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

int VEDemux::close()
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

shared_ptr<VEMediaInfo> VEDemux::getFileInfo()
{
    shared_ptr<VEMediaInfo> tmp = make_shared<VEMediaInfo>();

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
