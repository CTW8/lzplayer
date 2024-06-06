#ifndef __VE_PACKET__
#define __VE_PACKET__

extern "C"
{
    #include "libavcodec/avcodec.h"
}

typedef struct _VEPacket{
    int type;
    AVPacket *frame;
}VEPacket;

#endif