#ifndef __VE_AUDIO_DECODER__
#define __VE_AUDIO_DECODER__

#include<string>
#include<memory>

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}

#include"VEMediaDef.h"
#include"VEPacket.h"
#include"VEFrame.h"

class VEAudioDecoder{
public:
    VEAudioDecoder();
    ~VEAudioDecoder();

    ///init
    int init(VEMediaInfo *info);

    ///flush
    int flush();

    ///send packet
    int sendPacket(VEPacket *pack);

    ///decodeFrame
    int readFrame(VEFrame *frame);
    ///uninit
    int uninit();
private:
    /* data */
    AVCodecContext * mAudioCtx=nullptr;
    VEMediaInfo * mMediaInfo=nullptr;

};

#endif