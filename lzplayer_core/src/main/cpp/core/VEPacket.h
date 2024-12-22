#ifndef __VE_PACKET__
#define __VE_PACKET__

#include "Log.h"
#include "VEDef.h"
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

    void setPts(int64_t pts){
        this->pts = pts;
    }

    int64_t getPts(){
        return pts;
    }

    void setDts(int64_t dts){
        this->dts = dts;
    }

    int64_t getDts(){
        return dts;
    }

private:
    EPacketType ePacketType = E_PACKET_TYPE_UNKNOW;
    AVPacket * mPacket;
    int64_t pts;
    int64_t dts;
};

#endif