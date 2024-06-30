#ifndef __VE_PACKET__
#define __VE_PACKET__

#include "Log.h"

extern "C"
{
    #include "libavcodec/avcodec.h"
}

class VEPacket{
public:
    VEPacket(){
        mPacket = av_packet_alloc();
        if(!mPacket){
            ALOGE("Could not allocate AVPacket");
        }
    }

    VEPacket(VEPacket* p){
        mPacket = p->getPacket();
    }
    ~VEPacket(){
        if(mPacket){
            av_packet_unref(mPacket);
            av_packet_free(&mPacket);
        }
    }

    AVPacket& operator=(VEPacket &packet){
        av_packet_ref(mPacket,packet.getPacket());
    }

    AVPacket * getPacket(){
        return mPacket;
    }

private:
    AVPacket * mPacket;
};

#endif