#ifndef LZPLAYER_IMEDIASOURCE_H
#define LZPLAYER_IMEDIASOURCE_H

#include <memory>
#include "AMessage.h"
#include "VEError.h"
#include "core/VEPacket.h"
#include "core/VEMediaDef.h"

namespace VE {

    /// 媒体数据源的纯数据面接口：向下游解码器提供解复用后的码流包。
    ///
    /// 生命周期(prepare/start/stop/...)由持有具体实现的播放器直接管理，
    /// 不属于这里；解码器只透过本接口拉数据。
    /// 本地文件(VEDemux)以及后续的网络流(RTMP/HLS)都实现这个接口。
    class IMediaSource {
    public:
        virtual ~IMediaSource() = default;

        /// 取一个指定类型轨道的码流包。无数据时返回 VE_NOT_ENOUGH_DATA，
        /// 调用方按固定间隔轮询重试(仿 NuPlayer dequeueAccessUnit + 10ms retry)。
        /// 由解码器/字幕组件线程调用，实现方需自行保证线程安全。
        virtual VEResult read(ETrackType type, std::shared_ptr<VEPacket> &packet) = 0;

        /// 媒体信息(轨道列表/时长/编码参数等)，prepare 完成后才有效
        virtual std::shared_ptr<VEMediaInfo> getFileInfo() = 0;

        /// read 返回 VE_NOT_ENOUGH_DATA 之后登记一次性通知：该轨道有新
        /// 数据入队时 post 一次 notify 并清除登记。
        ///
        /// 取代"每 10ms 轮询一次"——轮询既有最高 10ms 的到达延迟，
        /// 又在饥饿期间持续唤醒线程。one-shot 语义防通知风暴。
        /// 默认实现为空(不支持通知的源，调用方会退化为轮询)。
        virtual void requestReadNotify(ETrackType type,
                                       const std::shared_ptr<AMessage> &notify) {
            (void) type;
            (void) notify;
        }
    };
}

#endif //LZPLAYER_IMEDIASOURCE_H
