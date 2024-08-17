#ifndef __VE_PACKET__
#define __VE_PACKET__

#include "Log.h"

extern "C"
{
    #include "libavcodec/avcodec.h"
}

enum EPacketType{
    E_PACKET_TYPE_UNKNOW = -1,
    E_PACKET_TYPE_VIDEO,
    E_PACKET_TYPE_AUDIO,
    E_PACKET_TYPE_EOF
};

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

    VEPacket& operator=(VEPacket &packet){
        av_packet_ref(mPacket,packet.getPacket());
        return *this;
    }

    AVPacket * getPacket(){
        return mPacket;
    }

    void setPacketType(EPacketType type){
        ePacketType = type;
    }

    EPacketType getPacketType(){
        return ePacketType;
    }

private:
    EPacketType ePacketType = E_PACKET_TYPE_UNKNOW;
    AVPacket * mPacket;
};

#endif