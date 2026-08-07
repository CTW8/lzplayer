#include "VEVideoDecoderFactory.h"

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
        return std::make_shared<VEVideoDecoder>(notify);
    }
}
