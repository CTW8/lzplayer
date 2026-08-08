#ifndef LZPLAYER_VEVIDEORENDERFACTORY_H
#define LZPLAYER_VEVIDEORENDERFACTORY_H

#include <memory>

#include "IVideoRender.h"

namespace VE {

    /// 视频渲染后端选择器：GLES(默认) 或 Vulkan。
    ///
    /// 只作用于**软解**路径。硬解由 MediaCodec 直出 Surface，两个后端都不经过，
    /// 所以要验证 Vulkan 必须先打开"强制软解"，否则根本走不到这里。
    ///
    /// 选择策略刻意保守：默认 GLES，Vulkan 需显式打开。Vulkan 在各家驱动上的
    /// 初始化失败面比 GLES 大得多，且它对本工程没有性能收益(软解瓶颈在解码，
    /// 不在纹理上传)，没有理由把它设成默认。
    class VEVideoRenderFactory {
    public:
        struct Policy {
            /// 显式要求 Vulkan。为假时一律 GLES，连 loader 都不去 dlopen
            bool preferVulkan = false;
        };

        /// 建一个已 initialize 成功的渲染器。
        ///
        /// 要求 Vulkan 但它建不起来时**自动回退 GLES**并把 outUsedVulkan 置假——
        /// 渲染后端失败不该让播放整体失败，但上层需要知道实际用的是哪个，
        /// 否则诊断面板会显示一个和事实不符的后端名。
        /// 两个后端都失败才返回 nullptr。
        static std::shared_ptr<IVideoRender> create(const VEBundle &params,
                                                    const Policy &policy,
                                                    bool *outUsedVulkan);

        /// 当前实际后端名，供 stats/诊断面板显示
        static const char *backendName(bool usedVulkan) {
            return usedVulkan ? "Vulkan" : "OpenGL ES";
        }
    };
}

#endif //LZPLAYER_VEVIDEORENDERFACTORY_H
