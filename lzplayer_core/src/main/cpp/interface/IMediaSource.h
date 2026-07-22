#ifndef LZPLAYER_IMEDIASOURCE_H
#define LZPLAYER_IMEDIASOURCE_H

#include <memory>
#include "IVEComponent.h"
#include "VEError.h"
#include "core/VEPacket.h"
#include "core/VEMediaDef.h"

namespace VE {

    /// 媒体数据源：向下游解码器提供解复用后的码流包。
    ///
    /// 采用拉模型：解码器主动 read()，取不到数据时用 needMorePacket() 登记
    /// 一条唤醒消息，数据到位后由数据源投递回去，从而形成反压。
    /// 本地文件(VEDemux)以及后续的网络流(RTMP/HLS)都实现这个接口。
    class IMediaSource : public virtual IVEComponent {
    public:
        ~IMediaSource() override = default;

        /// 取一个码流包。无数据时返回 VE_NOT_ENOUGH_DATA，由调用方登记唤醒消息。
        virtual VEResult read(bool isAudio, std::shared_ptr<VEPacket> &packet) = 0;

        /// 登记一条"有数据了就投递"的唤醒消息。type: 1=音频，其它=视频。
        /// 会被解码器线程调用，实现方需自行保证线程安全。
        virtual void needMorePacket(std::shared_ptr<AMessage> msg, int type) = 0;

        /// 媒体信息(时长/分辨率/采样率/编码参数等)，prepare 完成后才有效
        virtual std::shared_ptr<VEMediaInfo> getFileInfo() = 0;
    };
}

#endif //LZPLAYER_IMEDIASOURCE_H
