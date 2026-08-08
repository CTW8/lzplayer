#include "VEVulkanApi.h"

#include <dlfcn.h>
#include <mutex>

#include "VEJvmOnLoad.h"

namespace VE {

    static void *sLoaderHandle = nullptr;
    static bool sLoaderTried = false;
    static bool sLoaderOk = false;
    static std::mutex sLoaderMutex;

    VulkanApi &VulkanApi::get() {
        static VulkanApi sApi;
        return sApi;
    }

    bool VulkanApi::loadLoader() {
        std::lock_guard<std::mutex> lock(sLoaderMutex);
        if (sLoaderTried) {
            return sLoaderOk;
        }
        sLoaderTried = true;

        sLoaderHandle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (sLoaderHandle == nullptr) {
            ALOGW("VulkanApi: libvulkan.so not loadable (%s), Vulkan unavailable", dlerror());
            return false;
        }

        VulkanApi &api = get();
        api.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
                dlsym(sLoaderHandle, "vkGetInstanceProcAddr");
        if (api.vkGetInstanceProcAddr == nullptr) {
            ALOGE("VulkanApi: vkGetInstanceProcAddr missing, Vulkan unavailable");
            return false;
        }

        // instance 为 NULL 时只能取到全局函数，这是规范允许的用法
        int missing = 0;
#define VE_VK_LOAD_GLOBAL(name)                                                     \
        api.name = (PFN_##name) api.vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);   \
        if (api.name == nullptr) { ALOGE("VulkanApi: missing %s", #name); ++missing; }
        VE_VK_GLOBAL_FUNCS(VE_VK_LOAD_GLOBAL)
#undef VE_VK_LOAD_GLOBAL

        sLoaderOk = (missing == 0);
        ALOGI("VulkanApi: loader %s", sLoaderOk ? "ready" : "incomplete");
        return sLoaderOk;
    }

    bool VulkanApi::loaderAvailable() {
        return loadLoader();
    }

    bool VulkanApi::loadInstanceFuncs(VkInstance instance) {
        int missing = 0;
#define VE_VK_LOAD_INSTANCE(name)                                               \
        name = (PFN_##name) vkGetInstanceProcAddr(instance, #name);             \
        if (name == nullptr) { ALOGE("VulkanApi: missing %s", #name); ++missing; }
        VE_VK_INSTANCE_FUNCS(VE_VK_LOAD_INSTANCE)
#undef VE_VK_LOAD_INSTANCE
        if (missing > 0) {
            ALOGE("VulkanApi: %d instance funcs missing", missing);
            return false;
        }
        return true;
    }

    bool VulkanApi::loadDeviceFuncs(VkDevice device) {
        int missing = 0;
#define VE_VK_LOAD_DEVICE(name)                                                 \
        name = (PFN_##name) vkGetDeviceProcAddr(device, #name);                  \
        if (name == nullptr) { ALOGE("VulkanApi: missing %s", #name); ++missing; }
        VE_VK_DEVICE_FUNCS(VE_VK_LOAD_DEVICE)
#undef VE_VK_LOAD_DEVICE
        if (missing > 0) {
            ALOGE("VulkanApi: %d device funcs missing", missing);
            return false;
        }
        return true;
    }
}
