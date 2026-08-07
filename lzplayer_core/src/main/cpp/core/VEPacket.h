#ifndef __VE_PACKET__
#define __VE_PACKET__

#include "Log.h"
#include "VEDef.h"
extern "C"
{
    #include "libavcodec/avcodec.h"
}
namespace VE {
    class VEPacket {
    public:
        VEPacket() {
            mPacket = av_packet_alloc();
            if (!mPacket) {
                ALOGE("Could not allocate AVPacket");
            }
        }

        // 独占持有裸 AVPacket*，析构 unref+free。禁止拷贝/赋值：浅拷贝会让
        // 两个对象析构同一个 AVPacket 造成 double-free。一律经 shared_ptr 传递。
        VEPacket(const VEPacket &) = delete;
        VEPacket &operator=(const VEPacket &) = delete;

        ~VEPacket() {
            if (mPacket) {
                av_packet_unref(mPacket);
                av_packet_free(&mPacket);
            }
        }

        AVPacket *getPacket() {
            return mPacket;
        }

        void setPacketType(EPacketType type) {
            ePacketType = type;
        }

        EPacketType getPacketType() {
            return ePacketType;
        }

        void setPts(int64_t pts) {
            this->pts = pts;
        }

        int64_t getPts() {
            return pts;
        }

        void setDts(int64_t dts) {
            this->dts = dts;
        }

        int64_t getDts() {
            return dts;
        }

        /// 该包时长(微秒)，已由 demux 从流时间基 rescale。
        /// 容器未提供时为 0，用于队列的缓冲时长记账。
        void setDurationUs(int64_t durationUs) {
            this->durationUs = durationUs;
        }

        int64_t getDurationUs() {
            return durationUs;
        }

    private:
        EPacketType ePacketType = E_PACKET_TYPE_UNKNOW;
        AVPacket *mPacket;
        int64_t pts = 0;
        int64_t dts = 0;
        int64_t durationUs = 0;
    };
}
#endif