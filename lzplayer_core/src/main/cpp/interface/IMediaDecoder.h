#ifndef __VE_MEDIA_DECODER__
#define __VE_MEDIA_DECODER__

#include <memory>
#include "AMessage.h"
#include "VEError.h"
#include "core/VEFrame.h"

namespace VE {

    /// 解码器的纯数据面接口：向下游渲染器提供解码后的帧。
    ///
    /// 生命周期由持有具体实现的播放器直接管理。渲染链路只依赖这个接口，
    /// 因此软解(VEVideoDecoder/VEAudioDecoder)与后续的 MediaCodec 硬解可以互换。
    ///
    /// 注意：这里刻意传递 VEFrame(内部持有引用计数的 AVFrame)而不是裸字节数组，
    /// 否则每帧都要拷贝一次，正好抵消掉解码到渲染之间的零拷贝。
    class IMediaDecoder {
    public:
        virtual ~IMediaDecoder() = default;

        /// 取一帧已解码数据。无数据时返回 VE_NOT_ENOUGH_DATA。
        virtual VEResult readFrame(std::shared_ptr<VEFrame> &frame) = 0;

        /// 登记一条"有帧了就投递"的唤醒消息
        virtual void needMoreFrame(std::shared_ptr<AMessage> msg) = 0;
    };

} // namespace VE

#endif
