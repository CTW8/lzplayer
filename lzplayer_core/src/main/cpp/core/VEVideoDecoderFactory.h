#ifndef LZPLAYER_VEVIDEODECODERFACTORY_H
#define LZPLAYER_VEVIDEODECODERFACTORY_H

#include <memory>
#include <android/native_window.h>

#include "IMediaDecoder.h"
#include "VEAVsync.h"
#include "VEMediaDef.h"
#include "thread/AMessage.h"

namespace VE {

    /// 解码器选择策略。上层可强制走某一条路(调试、或已知机型有问题时)。
    struct DecoderPolicy {
        bool forceSoftware = false;
        bool forceHardware = false;
    };

    /// 视频解码器工厂：按轨道参数与策略选硬解或软解。
    ///
    /// 两者都实现 IMediaDecoder，播放器拿到后一视同仁；硬解组件会同时
    /// 占据 Role 表的解码与显示两个槽位(见 VEMediaCodecVideoDecoder)，
    /// 所以 outIsHardware 要回传给调用方以决定怎么登记角色。
    class VEVideoDecoderFactory {
    public:
        /// surface 为空、codec 不在白名单、或创建失败时自动退回软解，
        /// 因此本函数不会因为硬解不可用而失败。
        static std::shared_ptr<IMediaDecoder> create(
                const VETrackInfo &track,
                ANativeWindow *surface,
                const DecoderPolicy &policy,
                std::shared_ptr<AMessage> &notify,
                const std::shared_ptr<VEAVsync> &avSync,
                bool *outIsHardware);
    };
}

#endif //LZPLAYER_VEVIDEODECODERFACTORY_H
