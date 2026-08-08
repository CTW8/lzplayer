#include "VEVideoRenderFactory.h"

#include "VEGLESVideoRenderer.h"
#include "VEVulkanVideoRenderer.h"
#include "VEJvmOnLoad.h"

namespace VE {

    std::shared_ptr<IVideoRender> VEVideoRenderFactory::create(const VEBundle &params,
                                                               const Policy &policy,
                                                               bool *outUsedVulkan) {
        if (outUsedVulkan != nullptr) {
            *outUsedVulkan = false;
        }

        if (policy.preferVulkan) {
            if (!VEVulkanVideoRenderer::isAvailable()) {
                ALOGW("VEVideoRenderFactory: Vulkan requested but loader unavailable, "
                      "falling back to GLES");
            } else {
                auto vk = std::make_shared<VEVulkanVideoRenderer>();
                // initialize 内部已覆盖 instance/device/surface/swapchain 全过程，
                // 任一步失败都返回错误；不必在这里再做能力探测
                if (vk->initialize(params) == VE_OK) {
                    ALOGI("VEVideoRenderFactory: using Vulkan renderer");
                    if (outUsedVulkan != nullptr) {
                        *outUsedVulkan = true;
                    }
                    return vk;
                }
                // 失败的实例必须显式拆掉：它可能已经建了 instance/device，
                // 留给析构也行，但那要等 shared_ptr 出作用域，日志顺序会乱
                vk->uninitialize();
                ALOGW("VEVideoRenderFactory: Vulkan init failed, falling back to GLES");
            }
        }

        auto gles = std::make_shared<VEGLESVideoRenderer>();
        if (gles->initialize(params) != VE_OK) {
            ALOGE("VEVideoRenderFactory: GLES init failed too, no video render available");
            return nullptr;
        }
        ALOGI("VEVideoRenderFactory: using OpenGL ES renderer");
        return gles;
    }
}
