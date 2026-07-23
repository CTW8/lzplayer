#ifndef LZPLAYER_IFRAMESINK_H
#define LZPLAYER_IFRAMESINK_H

#include <memory>
#include "AMessage.h"
#include "core/VEFrame.h"

namespace VE {

    /// 帧接收端(渲染器)的推送接口，仿 NuPlayer Renderer::queueBuffer。
    ///
    /// 数据流向：解码器解出一帧就推给 sink；sink 渲染或丢弃该帧后，
    /// 原样投递 consumedReply 向解码器归还 credit——解码器以在途帧计数
    /// 为闸，credit 用尽自然停、归还自然续，流控是缓冲所有权循环的
    /// 内生性质，不需要显式的启停命令。
    ///
    /// 防过期：sink 实现内部持有 atomic 队列代次，queueFrame 投递时盖章、
    /// 接收时校验；consumedReply 由解码器构造并携带自身 epoch，双向各自
    /// 清算，flush/seek 后的过期帧与过期回执都无法穿透。
    ///
    /// 将来的 MediaCodec 硬解 output callback 天然是推，同样实现本接口。
    class IFrameSink {
    public:
        virtual ~IFrameSink() = default;

        /// 推一帧。consumedReply 可能为 null(EOS 哨兵等无需回执的场景不会
        /// 出现，正常帧必须携带)；sink 消费(渲染或丢弃)后原样 post。
        /// 由解码器线程调用，实现方需线程安全(通常转投自己的 looper)。
        virtual void queueFrame(const std::shared_ptr<VEFrame> &frame,
                                const std::shared_ptr<AMessage> &consumedReply) = 0;
    };
}

#endif //LZPLAYER_IFRAMESINK_H
