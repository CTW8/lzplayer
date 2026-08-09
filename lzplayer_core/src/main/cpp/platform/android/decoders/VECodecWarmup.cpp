#include "VECodecWarmup.h"

#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "VEJvmOnLoad.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

namespace VE {

    namespace {
        std::mutex sMutex;
        /// 预热出来的实例。nullptr 表示"没有可取的"
        AMediaCodec *sCodec = nullptr;
        std::string sMime;
        /// 后台创建是否在进行中。take() 靠它决定是否等待
        bool sInFlight = false;
        std::condition_variable sCv;
    }

    const char *VECodecWarmup::mimeForCodec(int avCodecId) {
        switch (avCodecId) {
            case AV_CODEC_ID_H264:
                return "video/avc";
            case AV_CODEC_ID_HEVC:
                return "video/hevc";
            default:
                return nullptr;
        }
    }

    void VECodecWarmup::warmUpForCodec(int avCodecId) {
        const char *mime = mimeForCodec(avCodecId);
        if (mime == nullptr) {
            return;   // 非硬解白名单，预热没有意义
        }
        const std::string wanted(mime);
        {
            std::lock_guard<std::mutex> lk(sMutex);
            if (sInFlight) {
                return;   // 已有在途预热，不重复起线程
            }
            if (sCodec != nullptr) {
                if (sMime == wanted) {
                    return;   // 槽位里就是要的那个
                }
                // mime 变了(换源)：旧的留着只是占资源
                AMediaCodec_delete(sCodec);
                sCodec = nullptr;
                sMime.clear();
            }
            sInFlight = true;
        }

        // detach：预热是"能赶上就赶上"的优化，不该让 prepare 等它
        std::thread([wanted]() {
            AMediaCodec *codec = AMediaCodec_createDecoderByType(wanted.c_str());
            {
                std::lock_guard<std::mutex> lk(sMutex);
                sInFlight = false;
                if (codec == nullptr) {
                    ALOGW("VECodecWarmup: createDecoderByType(%s) failed, "
                          "decoder will create its own", wanted.c_str());
                } else if (sCodec != nullptr) {
                    // discard() 期间竞态：槽位已被别人填上，丢掉这个
                    AMediaCodec_delete(codec);
                    codec = nullptr;
                } else {
                    sCodec = codec;
                    sMime = wanted;
                    ALOGI("VECodecWarmup: %s ready", wanted.c_str());
                }
            }
            sCv.notify_all();
        }).detach();
    }

    AMediaCodec *VECodecWarmup::take(const char *mime) {
        if (mime == nullptr) {
            return nullptr;
        }
        std::unique_lock<std::mutex> lk(sMutex);
        // 等在途预热完成：那 50ms 已经走了一部分，等完仍比从零创建快。
        // 只有 mime 对得上才值得等——不匹配的话等它毫无意义。
        sCv.wait(lk, [] { return !sInFlight; });
        if (sCodec == nullptr || sMime != mime) {
            return nullptr;
        }
        AMediaCodec *codec = sCodec;
        // 所有权移交调用方，槽位立即置空，避免被第二次取走造成双重释放
        sCodec = nullptr;
        sMime.clear();
        ALOGI("VECodecWarmup: handed over warm %s", mime);
        return codec;
    }

    void VECodecWarmup::discard() {
        std::unique_lock<std::mutex> lk(sMutex);
        sCv.wait(lk, [] { return !sInFlight; });
        if (sCodec != nullptr) {
            ALOGI("VECodecWarmup: discarding unused warm codec %s", sMime.c_str());
            AMediaCodec_delete(sCodec);
            sCodec = nullptr;
            sMime.clear();
        }
    }
}
