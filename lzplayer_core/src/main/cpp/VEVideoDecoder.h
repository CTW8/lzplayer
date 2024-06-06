#ifndef __VE_VIDEO_DECODER__
#define __VE_VIDEO_DECODER__

#include<memory>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include"VEFrame.h"
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}


class VEVideoDecoder
{

public:
    VEVideoDecoder(/* args */);
    ~VEVideoDecoder();

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
    AVCodecContext * mVideoCtx=nullptr;
    VEMediaInfo * mMediaInfo=nullptr;
};


#endif