#ifndef     __VE_MEDIA_DEF__
#define     __VE_MEDIA_DEF__
extern "C"{
#include"libavcodec/avcodec.h"
#include"libavformat/avformat.h"
}

typedef struct __VEMediaInfo{
    int width;
    int height;
    int fps;
    uint64_t duration;
    
    int channels;
    int sampleRate;
    int sampleFormat;

    AVCodecParameters *mAudioCodecParams;
    int audio_stream_index=0;
    AVRational mAudioTimeBase;
    int64_t mAStartTime=0;

    AVCodecParameters *mVideoCodecParams;
    int video_stream_index=0;
    AVRational mVideoTimeBase;
    int64_t mVStartTime=0;

}VEMediaInfo;


typedef enum _MediaState{
    
};


#endif