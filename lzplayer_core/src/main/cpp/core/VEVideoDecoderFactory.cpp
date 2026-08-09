#include "VEVideoDecoderFactory.h"
#include "platform/android/decoders/VECodecWarmup.h"

#include "VEVideoDecoder.h"
#include "decoders/VEMediaCodecVideoDecoder.h"
#include "utils/Log.h"

namespace VE {

    std::shared_ptr<IMediaDecoder> VEVideoDecoderFactory::create(
            const VETrackInfo &track,
            ANativeWindow *surface,
            const DecoderPolicy &policy,
            std::shared_ptr<AMessage> &notify,
            const std::shared_ptr<VEAVsync> &avSync,
            bool *outIsHardware) {
        if (outIsHardware) {
            *outIsHardware = false;
        }

        // 硬解的前提：没被强制软解、codec 在白名单、且有 Surface 可直出。
        // 缺任何一条都不是错误，安静地走软解即可。
        const bool wantHardware =
                !policy.forceSoftware &&
                surface != nullptr &&
                VEMediaCodecVideoDecoder::isSupported(track);

        if (wantHardware) {
            auto hw = std::make_shared<VEMediaCodecVideoDecoder>(notify, avSync);
            // 真正的创建失败(厂商 codec 起不来)发生在 prepare 里，
            // 那时会经 notify 上报带 fallback 标记的错误，由播放器重建为软解
            if (outIsHardware) {
                *outIsHardware = true;
            }
            ALOGI("VEVideoDecoderFactory::%s hardware decoder for codec %d",
                  __FUNCTION__, track.codecId);
            return hw;
        }

        if (policy.forceHardware) {
            ALOGW("VEVideoDecoderFactory::%s forceHardware requested but unavailable"
                  " (surface=%p codec=%d), falling back to software",
                  __FUNCTION__, (void *) surface, track.codecId);
        }
        ALOGI("VEVideoDecoderFactory::%s software decoder for codec %d",
              __FUNCTION__, track.codecId);
        // 走软解就没人来取那个预热实例了。在这里就丢掉而不是等 onRelease——
        // MediaCodec 实例是有限的系统资源，整段播放都握着不用会挤掉其它
        // 应用的分配。预热是在 open_input 之后按 codec_id 无条件发起的，
        // 那时还不知道最终会选软解(强制软解开关、无 surface、硬解建链失败)。
        VECodecWarmup::discard();
        return std::make_shared<VEVideoDecoder>(notify);
    }
}
