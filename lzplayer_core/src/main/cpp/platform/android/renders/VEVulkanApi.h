#ifndef LZPLAYER_VEVULKANAPI_H
#define LZPLAYER_VEVULKANAPI_H

#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

namespace VE {

    /// Vulkan 入口表：全部经 dlopen("libvulkan.so") + vkGetInstanceProcAddr 取得，
    /// **不直接链接 libvulkan**。
    ///
    /// 为什么绕这一圈：直接把 vulkan 写进 target_link_libraries 会让 libvulkan.so
    /// 变成 libplayer.so 的 DT_NEEDED 硬依赖——一旦某台设备上它缺失或加载失败，
    /// 整个 native 库都装不进来，播放器直接起不来，而不是"退回 GLES"。之前
    /// libaaudio 就是这么踩的坑(API 26 的 stub 让 API 24/25 设备全部加载失败)。
    /// Vulkan 的 loader 虽然从 Android 7.0(API 24)起就是平台组成部分，理论上
    /// minSdk 24 都有，但渲染后端本就设计成可回退的，没理由为它把整个库押上去。
    ///
    /// 用法：所有调用都必须走 mVk.xxx(...)，绝不能直接写 vkXxx(...)——后者会
    /// 让链接器重新引入符号依赖，把上面这层保护白白抵消掉。
    struct VulkanApi {
        // 载入阶段一：dlsym 只取这一个符号，其余全部由它派生
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

        // 载入阶段二：instance 还不存在时可取的全局函数
#define VE_VK_GLOBAL_FUNCS(X)                  \
        X(vkCreateInstance)                    \
        X(vkEnumerateInstanceExtensionProperties)

        // 载入阶段三：有了 instance 之后可取的函数(含 WSI 扩展)
#define VE_VK_INSTANCE_FUNCS(X)                        \
        X(vkDestroyInstance)                           \
        X(vkEnumeratePhysicalDevices)                  \
        X(vkGetPhysicalDeviceProperties)               \
        X(vkGetPhysicalDeviceQueueFamilyProperties)    \
        X(vkGetPhysicalDeviceMemoryProperties)         \
        X(vkGetPhysicalDeviceFormatProperties)         \
        X(vkEnumerateDeviceExtensionProperties)        \
        X(vkCreateDevice)                              \
        X(vkGetDeviceProcAddr)                         \
        X(vkCreateAndroidSurfaceKHR)                   \
        X(vkDestroySurfaceKHR)                         \
        X(vkGetPhysicalDeviceSurfaceSupportKHR)        \
        X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)   \
        X(vkGetPhysicalDeviceSurfaceFormatsKHR)        \
        X(vkGetPhysicalDeviceSurfacePresentModesKHR)

        // 载入阶段四：设备级函数，经 vkGetDeviceProcAddr 取(比 instance 版少一层派发)
#define VE_VK_DEVICE_FUNCS(X)                  \
        X(vkDestroyDevice)                     \
        X(vkGetDeviceQueue)                    \
        X(vkDeviceWaitIdle)                    \
        X(vkQueueSubmit)                       \
        X(vkQueueWaitIdle)                     \
        X(vkQueuePresentKHR)                   \
        X(vkCreateSwapchainKHR)                \
        X(vkDestroySwapchainKHR)               \
        X(vkGetSwapchainImagesKHR)             \
        X(vkAcquireNextImageKHR)               \
        X(vkCreateImageView)                   \
        X(vkDestroyImageView)                  \
        X(vkCreateRenderPass)                  \
        X(vkDestroyRenderPass)                 \
        X(vkCreateFramebuffer)                 \
        X(vkDestroyFramebuffer)                \
        X(vkCreateShaderModule)                \
        X(vkDestroyShaderModule)               \
        X(vkCreatePipelineLayout)              \
        X(vkDestroyPipelineLayout)             \
        X(vkCreateGraphicsPipelines)           \
        X(vkDestroyPipeline)                   \
        X(vkCreateDescriptorSetLayout)         \
        X(vkDestroyDescriptorSetLayout)        \
        X(vkCreateDescriptorPool)              \
        X(vkDestroyDescriptorPool)             \
        X(vkAllocateDescriptorSets)            \
        X(vkUpdateDescriptorSets)              \
        X(vkCreateSampler)                     \
        X(vkDestroySampler)                    \
        X(vkCreateCommandPool)                 \
        X(vkDestroyCommandPool)                \
        X(vkAllocateCommandBuffers)            \
        X(vkFreeCommandBuffers)                \
        X(vkBeginCommandBuffer)                \
        X(vkEndCommandBuffer)                  \
        X(vkResetCommandBuffer)                \
        X(vkCreateSemaphore)                   \
        X(vkDestroySemaphore)                  \
        X(vkCreateFence)                       \
        X(vkDestroyFence)                      \
        X(vkWaitForFences)                     \
        X(vkResetFences)                       \
        X(vkCreateBuffer)                      \
        X(vkDestroyBuffer)                     \
        X(vkCreateImage)                       \
        X(vkDestroyImage)                      \
        X(vkGetBufferMemoryRequirements)       \
        X(vkGetImageMemoryRequirements)        \
        X(vkAllocateMemory)                    \
        X(vkFreeMemory)                        \
        X(vkBindBufferMemory)                  \
        X(vkBindImageMemory)                   \
        X(vkMapMemory)                         \
        X(vkUnmapMemory)                       \
        X(vkCmdBeginRenderPass)                \
        X(vkCmdEndRenderPass)                  \
        X(vkCmdBindPipeline)                   \
        X(vkCmdBindDescriptorSets)             \
        X(vkCmdBindVertexBuffers)              \
        X(vkCmdDraw)                           \
        X(vkCmdPushConstants)                  \
        X(vkCmdPipelineBarrier)                \
        X(vkCmdCopyBufferToImage)              \
        X(vkCmdSetViewport)                    \
        X(vkCmdSetScissor)

#define VE_VK_DECLARE(name) PFN_##name name = nullptr;
        VE_VK_GLOBAL_FUNCS(VE_VK_DECLARE)
        VE_VK_INSTANCE_FUNCS(VE_VK_DECLARE)
        VE_VK_DEVICE_FUNCS(VE_VK_DECLARE)
#undef VE_VK_DECLARE

        /// dlopen 加载器并取 vkGetInstanceProcAddr + 全局函数。
        /// 只做一次，成功后进程内一直有效(句柄故意不 dlclose)。
        static bool loadLoader();
        /// 拿到 instance 后补齐 instance 级函数
        bool loadInstanceFuncs(VkInstance instance);
        /// 拿到 device 后补齐设备级函数
        bool loadDeviceFuncs(VkDevice device);

        /// 进程唯一的入口表。loadLoader() 成功后才可用。
        static VulkanApi &get();
        /// 加载器是否可用(轻量，只看 dlopen 结果，不建 instance)
        static bool loaderAvailable();
    };
}

#endif //LZPLAYER_VEVULKANAPI_H
