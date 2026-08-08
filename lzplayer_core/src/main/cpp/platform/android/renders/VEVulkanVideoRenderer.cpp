#include "VEVulkanVideoRenderer.h"

#include <cstring>
#include <algorithm>

#include "VEVulkanApi.h"
#include "VEVulkanShaders.h"
#include "VEJvmOnLoad.h"

namespace VE {

    /// 全部调用都走这个引用，绝不直写 vkXxx —— 见 VEVulkanApi.h 顶部说明
#define VK (VulkanApi::get())

    /// Vulkan 调用失败就地返回。渲染后端可回退，遇错早退比硬撑更安全
#define VK_CHECK(expr, what)                                            \
    do {                                                                \
        VkResult _r = (expr);                                           \
        if (_r != VK_SUCCESS) {                                         \
            ALOGE("VEVulkanVideoRenderer: %s failed, VkResult=%d", what, _r); \
            return VE_UNKNOWN_ERROR;                                    \
        }                                                               \
    } while (0)

    VEVulkanVideoRenderer::VEVulkanVideoRenderer() {
        ALOGD("VEVulkanVideoRenderer constructed");
    }

    VEVulkanVideoRenderer::~VEVulkanVideoRenderer() {
        uninitialize();
        ALOGD("VEVulkanVideoRenderer destructed");
    }

    bool VEVulkanVideoRenderer::isAvailable() {
        // 只看 loader 能不能 dlopen。真正有没有可用物理设备要到 initialize
        // 才知道——那里失败会返回错误，由工厂回退到 GLES，不需要在这里提前建
        // 一个 instance 去试探(那要几十毫秒，且本身也可能崩在厂商驱动里)。
        return VulkanApi::loaderAvailable();
    }

    // ————————————————————————— 生命周期 —————————————————————————

    VEResult VEVulkanVideoRenderer::initialize(VEBundle params) {
        ALOGI("VEVulkanVideoRenderer::initialize");

        mWin = params.get<ANativeWindow *>("surface");
        mViewWidth = params.get<int>("width");
        mViewHeight = params.get<int>("height");
        mRotationDegrees = params.get<int>("rotation");

        if (mWin == nullptr) {
            ALOGE("VEVulkanVideoRenderer::initialize invalid surface");
            return VE_INVALID_PARAMS;
        }
        if (!VulkanApi::loadLoader()) {
            ALOGE("VEVulkanVideoRenderer::initialize vulkan loader unavailable");
            return VE_INVALID_OPERATION;
        }

        VEResult ret;
        if ((ret = createInstance()) != VE_OK) { return ret; }
        if ((ret = pickPhysicalDeviceAndQueue()) != VE_OK) { return ret; }
        if ((ret = createDevice()) != VE_OK) { return ret; }
        if ((ret = createSamplersAndDescriptorLayout()) != VE_OK) { return ret; }
        if ((ret = createCommandPool()) != VE_OK) { return ret; }
        if ((ret = createSyncObjects()) != VE_OK) { return ret; }
        mCoreReady = true;

        if ((ret = createSurface(mWin)) != VE_OK) { return ret; }
        if ((ret = createSwapchain()) != VE_OK) { return ret; }
        if ((ret = createRenderPass()) != VE_OK) { return ret; }
        if ((ret = createPipeline()) != VE_OK) { return ret; }
        if ((ret = createFramebuffers()) != VE_OK) { return ret; }
        if ((ret = ensureVertexBuffer()) != VE_OK) { return ret; }
        mSwapchainReady = true;

        ALOGI("VEVulkanVideoRenderer::initialize success, view %dx%d rotation %d, swap %ux%u",
              mViewWidth, mViewHeight, mRotationDegrees, mSwapExtent.width, mSwapExtent.height);
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::changeSurface(ANativeWindow *win, int viewWidth, int viewHeight) {
        ALOGI("VEVulkanVideoRenderer::changeSurface new surface %p, %dx%d",
              (void *) win, viewWidth, viewHeight);

        if (!mCoreReady) {
            ALOGE("VEVulkanVideoRenderer::changeSurface core not ready");
            return VE_OK;
        }

        // 换 surface 前必须等 GPU 把在飞的命令跑完：swapchain 与 framebuffer
        // 还被上一帧的命令缓冲引用着，直接销毁会踩到正在使用的对象
        VK.vkDeviceWaitIdle(mDevice);

        mSwapchainReady = false;
        destroySwapchainResources();
        if (mSurface != VK_NULL_HANDLE) {
            VK.vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
            mSurface = VK_NULL_HANDLE;
        }

        mWin = win;
        mViewWidth = viewWidth;
        mViewHeight = viewHeight;

        if (win == nullptr) {
            // surface 已销毁：资源释放到此为止，device 保留。下次拿到新 surface
            // 再重建 swapchain 即可，没必要把 instance/device 也推倒重来。
            ALOGI("VEVulkanVideoRenderer::changeSurface surface detached");
            return VE_OK;
        }

        VEResult ret;
        if ((ret = createSurface(win)) != VE_OK) { return ret; }
        if ((ret = createSwapchain()) != VE_OK) { return ret; }
        if ((ret = createRenderPass()) != VE_OK) { return ret; }
        if ((ret = createPipeline()) != VE_OK) { return ret; }
        if ((ret = createFramebuffers()) != VE_OK) { return ret; }
        mSwapchainReady = true;

        // 视口变了，顶点数据(fit-inside 缩放)下一帧必须重算
        mVertexRotation = -1;
        ALOGI("VEVulkanVideoRenderer::changeSurface success, swap %ux%u",
              mSwapExtent.width, mSwapExtent.height);
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::uninitialize() {
        if (!mCoreReady && mInstance == VK_NULL_HANDLE) {
            return VE_OK;
        }
        ALOGI("VEVulkanVideoRenderer::uninitialize");

        if (mDevice != VK_NULL_HANDLE) {
            VK.vkDeviceWaitIdle(mDevice);
        }
        mSwapchainReady = false;
        mCoreReady = false;

        destroySwapchainResources();
        destroyPlaneResources();

        if (mDevice != VK_NULL_HANDLE) {
            if (mVertexBuffer != VK_NULL_HANDLE) {
                if (mVertexMapped != nullptr) {
                    VK.vkUnmapMemory(mDevice, mVertexMemory);
                    mVertexMapped = nullptr;
                }
                VK.vkDestroyBuffer(mDevice, mVertexBuffer, nullptr);
                VK.vkFreeMemory(mDevice, mVertexMemory, nullptr);
                mVertexBuffer = VK_NULL_HANDLE;
                mVertexMemory = VK_NULL_HANDLE;
            }
            if (mInFlight != VK_NULL_HANDLE) {
                VK.vkDestroyFence(mDevice, mInFlight, nullptr);
                mInFlight = VK_NULL_HANDLE;
            }
            if (mImageAvailable != VK_NULL_HANDLE) {
                VK.vkDestroySemaphore(mDevice, mImageAvailable, nullptr);
                mImageAvailable = VK_NULL_HANDLE;
            }
            if (mRenderFinished != VK_NULL_HANDLE) {
                VK.vkDestroySemaphore(mDevice, mRenderFinished, nullptr);
                mRenderFinished = VK_NULL_HANDLE;
            }
            if (mDescPool != VK_NULL_HANDLE) {
                // 描述符集随池一起回收，不用单独 free
                VK.vkDestroyDescriptorPool(mDevice, mDescPool, nullptr);
                mDescPool = VK_NULL_HANDLE;
                mDescSet = VK_NULL_HANDLE;
            }
            if (mDescLayout != VK_NULL_HANDLE) {
                VK.vkDestroyDescriptorSetLayout(mDevice, mDescLayout, nullptr);
                mDescLayout = VK_NULL_HANDLE;
            }
            if (mSampler != VK_NULL_HANDLE) {
                VK.vkDestroySampler(mDevice, mSampler, nullptr);
                mSampler = VK_NULL_HANDLE;
            }
            if (mCmdPool != VK_NULL_HANDLE) {
                VK.vkDestroyCommandPool(mDevice, mCmdPool, nullptr);
                mCmdPool = VK_NULL_HANDLE;
                mCmdBuffer = VK_NULL_HANDLE;
            }
            VK.vkDestroyDevice(mDevice, nullptr);
            mDevice = VK_NULL_HANDLE;
        }

        if (mSurface != VK_NULL_HANDLE) {
            VK.vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
            mSurface = VK_NULL_HANDLE;
        }
        if (mInstance != VK_NULL_HANDLE) {
            VK.vkDestroyInstance(mInstance, nullptr);
            mInstance = VK_NULL_HANDLE;
        }
        mPhysical = VK_NULL_HANDLE;
        mWin = nullptr;
        return VE_OK;
    }

    // ————————————————————————— 一次性资源 —————————————————————————

    VEResult VEVulkanVideoRenderer::createInstance() {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "lzplayer";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "lzplayer_core";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        // 只用 1.0 的能力，要求高版本会在老驱动上直接建不出 instance
        app.apiVersion = VK_API_VERSION_1_0;

        const char *exts[] = {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        };

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = 2;
        ci.ppEnabledExtensionNames = exts;

        VK_CHECK(VK.vkCreateInstance(&ci, nullptr, &mInstance), "vkCreateInstance");
        if (!VulkanApi::get().loadInstanceFuncs(mInstance)) {
            return VE_INVALID_OPERATION;
        }
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::pickPhysicalDeviceAndQueue() {
        uint32_t count = 0;
        VK_CHECK(VK.vkEnumeratePhysicalDevices(mInstance, &count, nullptr),
                 "vkEnumeratePhysicalDevices");
        if (count == 0) {
            ALOGE("VEVulkanVideoRenderer: no Vulkan physical device");
            return VE_INVALID_OPERATION;
        }
        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(VK.vkEnumeratePhysicalDevices(mInstance, &count, devices.data()),
                 "vkEnumeratePhysicalDevices");

        // 移动端基本只有一块 GPU，取第一块同时满足"有图形队列"的即可
        for (VkPhysicalDevice dev : devices) {
            uint32_t qCount = 0;
            VK.vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
            if (qCount == 0) { continue; }
            std::vector<VkQueueFamilyProperties> families(qCount);
            VK.vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, families.data());

            for (uint32_t i = 0; i < qCount; ++i) {
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) { continue; }
                mPhysical = dev;
                mQueueFamily = i;
                VkPhysicalDeviceProperties props{};
                VK.vkGetPhysicalDeviceProperties(dev, &props);
                ALOGI("VEVulkanVideoRenderer: using GPU \"%s\", queue family %u",
                      props.deviceName, i);
                return VE_OK;
            }
        }
        ALOGE("VEVulkanVideoRenderer: no graphics-capable queue family");
        return VE_INVALID_OPERATION;
    }

    VEResult VEVulkanVideoRenderer::createDevice() {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = mQueueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;

        const char *exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &qci;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = exts;

        VK_CHECK(VK.vkCreateDevice(mPhysical, &ci, nullptr, &mDevice), "vkCreateDevice");
        if (!VulkanApi::get().loadDeviceFuncs(mDevice)) {
            return VE_INVALID_OPERATION;
        }
        VK.vkGetDeviceQueue(mDevice, mQueueFamily, 0, &mQueue);
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createSamplersAndDescriptorLayout() {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        // CLAMP_TO_EDGE：色度平面按 (w+1)/2 取整后边缘可能多出半个像素，
        // 用 REPEAT 会把对侧边缘绕回来，在画面边上拉出一条异色线
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxAnisotropy = 1.0f;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        VK_CHECK(VK.vkCreateSampler(mDevice, &si, nullptr, &mSampler), "vkCreateSampler");

        VkDescriptorSetLayoutBinding bindings[3]{};
        for (int i = 0; i < 3; ++i) {
            bindings[i].binding = static_cast<uint32_t>(i);
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 3;
        li.pBindings = bindings;
        VK_CHECK(VK.vkCreateDescriptorSetLayout(mDevice, &li, nullptr, &mDescLayout),
                 "vkCreateDescriptorSetLayout");

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 3;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &poolSize;
        VK_CHECK(VK.vkCreateDescriptorPool(mDevice, &pi, nullptr, &mDescPool),
                 "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = mDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &mDescLayout;
        VK_CHECK(VK.vkAllocateDescriptorSets(mDevice, &ai, &mDescSet),
                 "vkAllocateDescriptorSets");
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createCommandPool() {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = mQueueFamily;
        // 每帧重录同一个命令缓冲，必须允许单独 reset
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(VK.vkCreateCommandPool(mDevice, &ci, nullptr, &mCmdPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = mCmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(VK.vkAllocateCommandBuffers(mDevice, &ai, &mCmdBuffer),
                 "vkAllocateCommandBuffers");
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createSyncObjects() {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(VK.vkCreateSemaphore(mDevice, &si, nullptr, &mImageAvailable),
                 "vkCreateSemaphore(imageAvailable)");
        VK_CHECK(VK.vkCreateSemaphore(mDevice, &si, nullptr, &mRenderFinished),
                 "vkCreateSemaphore(renderFinished)");

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // 起始置为 signaled：第一帧开头就 wait，否则会永久卡住
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(VK.vkCreateFence(mDevice, &fi, nullptr, &mInFlight), "vkCreateFence");
        return VE_OK;
    }

    // ———————————————————— surface 相关资源 ————————————————————

    VEResult VEVulkanVideoRenderer::createSurface(ANativeWindow *win) {
        VkAndroidSurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        ci.window = win;
        VK_CHECK(VK.vkCreateAndroidSurfaceKHR(mInstance, &ci, nullptr, &mSurface),
                 "vkCreateAndroidSurfaceKHR");

        VkBool32 supported = VK_FALSE;
        VK_CHECK(VK.vkGetPhysicalDeviceSurfaceSupportKHR(mPhysical, mQueueFamily, mSurface,
                                                         &supported),
                 "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (!supported) {
            ALOGE("VEVulkanVideoRenderer: queue family %u cannot present to this surface",
                  mQueueFamily);
            return VE_INVALID_OPERATION;
        }
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createSwapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        VK_CHECK(VK.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysical, mSurface, &caps),
                 "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t fmtCount = 0;
        VK_CHECK(VK.vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysical, mSurface, &fmtCount, nullptr),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (fmtCount == 0) {
            ALOGE("VEVulkanVideoRenderer: surface reports no formats");
            return VE_INVALID_OPERATION;
        }
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        VK_CHECK(VK.vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysical, mSurface, &fmtCount,
                                                         formats.data()),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR");

        // 优先 R8G8B8A8_UNORM + SRGB_NONLINEAR。着色器输出的是已经做完
        // YUV→RGB 的线性值，选 *_SRGB 格式会让驱动再做一次 sRGB 编码，画面发白
        VkSurfaceFormatKHR chosen = formats[0];
        for (const VkSurfaceFormatKHR &f : formats) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f;
                break;
            }
        }
        mSwapFormat = chosen.format;

        if (caps.currentExtent.width != UINT32_MAX) {
            mSwapExtent = caps.currentExtent;
        } else {
            mSwapExtent.width = std::max(caps.minImageExtent.width,
                                         std::min(caps.maxImageExtent.width,
                                                  static_cast<uint32_t>(mViewWidth)));
            mSwapExtent.height = std::max(caps.minImageExtent.height,
                                          std::min(caps.maxImageExtent.height,
                                                   static_cast<uint32_t>(mViewHeight)));
        }
        if (mSwapExtent.width == 0 || mSwapExtent.height == 0) {
            ALOGE("VEVulkanVideoRenderer: degenerate swapchain extent %ux%u",
                  mSwapExtent.width, mSwapExtent.height);
            return VE_UNKNOWN_ERROR;
        }

        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = mSurface;
        ci.minImageCount = imageCount;
        ci.imageFormat = mSwapFormat;
        ci.imageColorSpace = chosen.colorSpace;
        ci.imageExtent = mSwapExtent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        // FIFO 是唯一被规范保证支持的模式，且自带垂直同步。帧率由上层的
        // 音视频同步决定，这里不需要 MAILBOX 那种"尽快出图"的语义
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        ci.clipped = VK_TRUE;
        VK_CHECK(VK.vkCreateSwapchainKHR(mDevice, &ci, nullptr, &mSwapchain),
                 "vkCreateSwapchainKHR");

        uint32_t actual = 0;
        VK_CHECK(VK.vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actual, nullptr),
                 "vkGetSwapchainImagesKHR");
        mSwapImages.resize(actual);
        VK_CHECK(VK.vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actual, mSwapImages.data()),
                 "vkGetSwapchainImagesKHR");

        mSwapViews.resize(actual);
        for (uint32_t i = 0; i < actual; ++i) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = mSwapImages[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = mSwapFormat;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            VK_CHECK(VK.vkCreateImageView(mDevice, &vi, nullptr, &mSwapViews[i]),
                     "vkCreateImageView(swapchain)");
        }
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createRenderPass() {
        VkAttachmentDescription color{};
        color.format = mSwapFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        // 每帧铺满画面之外还有黑边(fit-inside)，必须 CLEAR，否则残留上一帧
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        // 等 acquire 的信号量放行后再写颜色附件
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &subpass;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;
        VK_CHECK(VK.vkCreateRenderPass(mDevice, &ci, nullptr, &mRenderPass),
                 "vkCreateRenderPass");
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createPipeline() {
        VkShaderModule vert = VK_NULL_HANDLE;
        VkShaderModule frag = VK_NULL_HANDLE;

        VkShaderModuleCreateInfo vsi{};
        vsi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsi.codeSize = sizeof(vkshaders::kYuvVert);
        vsi.pCode = vkshaders::kYuvVert;
        VK_CHECK(VK.vkCreateShaderModule(mDevice, &vsi, nullptr, &vert),
                 "vkCreateShaderModule(vert)");

        VkShaderModuleCreateInfo fsi{};
        fsi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fsi.codeSize = sizeof(vkshaders::kYuvFrag);
        fsi.pCode = vkshaders::kYuvFrag;
        if (VK.vkCreateShaderModule(mDevice, &fsi, nullptr, &frag) != VK_SUCCESS) {
            VK.vkDestroyShaderModule(mDevice, vert, nullptr);
            ALOGE("VEVulkanVideoRenderer: vkCreateShaderModule(frag) failed");
            return VE_UNKNOWN_ERROR;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = 4 * sizeof(float);   // vec2 pos + vec2 uv
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = 2 * sizeof(float);

        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = 2;
        vin.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        VkViewport viewport{};
        viewport.width = static_cast<float>(mSwapExtent.width);
        viewport.height = static_cast<float>(mSwapExtent.height);
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = mSwapExtent;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.pViewports = &viewport;
        vp.scissorCount = 1;
        vp.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        // 旋转 90/270 时纹理坐标换序会翻转三角形绕向，开背面剔除会整片消失
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        ms.minSampleShading = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttach{};
        blendAttach.blendEnable = VK_FALSE;
        blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &blendAttach;

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(ColorParams);

        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &mDescLayout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcr;
        if (VK.vkCreatePipelineLayout(mDevice, &pli, nullptr, &mPipelineLayout) != VK_SUCCESS) {
            VK.vkDestroyShaderModule(mDevice, vert, nullptr);
            VK.vkDestroyShaderModule(mDevice, frag, nullptr);
            ALOGE("VEVulkanVideoRenderer: vkCreatePipelineLayout failed");
            return VE_UNKNOWN_ERROR;
        }

        VkGraphicsPipelineCreateInfo gpi{};
        gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpi.stageCount = 2;
        gpi.pStages = stages;
        gpi.pVertexInputState = &vin;
        gpi.pInputAssemblyState = &ia;
        gpi.pViewportState = &vp;
        gpi.pRasterizationState = &rs;
        gpi.pMultisampleState = &ms;
        gpi.pColorBlendState = &cb;
        gpi.layout = mPipelineLayout;
        gpi.renderPass = mRenderPass;
        gpi.subpass = 0;

        VkResult r = VK.vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &gpi, nullptr,
                                                  &mPipeline);
        // shader module 只在建管线时被读取，之后可以立刻释放
        VK.vkDestroyShaderModule(mDevice, vert, nullptr);
        VK.vkDestroyShaderModule(mDevice, frag, nullptr);
        if (r != VK_SUCCESS) {
            ALOGE("VEVulkanVideoRenderer: vkCreateGraphicsPipelines failed, VkResult=%d", r);
            return VE_UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    VEResult VEVulkanVideoRenderer::createFramebuffers() {
        mFramebuffers.resize(mSwapViews.size());
        for (size_t i = 0; i < mSwapViews.size(); ++i) {
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = mRenderPass;
            ci.attachmentCount = 1;
            ci.pAttachments = &mSwapViews[i];
            ci.width = mSwapExtent.width;
            ci.height = mSwapExtent.height;
            ci.layers = 1;
            VK_CHECK(VK.vkCreateFramebuffer(mDevice, &ci, nullptr, &mFramebuffers[i]),
                     "vkCreateFramebuffer");
        }
        return VE_OK;
    }

    void VEVulkanVideoRenderer::destroySwapchainResources() {
        if (mDevice == VK_NULL_HANDLE) {
            return;
        }
        for (VkFramebuffer fb : mFramebuffers) {
            if (fb != VK_NULL_HANDLE) { VK.vkDestroyFramebuffer(mDevice, fb, nullptr); }
        }
        mFramebuffers.clear();

        if (mPipeline != VK_NULL_HANDLE) {
            VK.vkDestroyPipeline(mDevice, mPipeline, nullptr);
            mPipeline = VK_NULL_HANDLE;
        }
        if (mPipelineLayout != VK_NULL_HANDLE) {
            VK.vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
            mPipelineLayout = VK_NULL_HANDLE;
        }
        if (mRenderPass != VK_NULL_HANDLE) {
            VK.vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
            mRenderPass = VK_NULL_HANDLE;
        }
        for (VkImageView view : mSwapViews) {
            if (view != VK_NULL_HANDLE) { VK.vkDestroyImageView(mDevice, view, nullptr); }
        }
        mSwapViews.clear();
        // swapchain image 由 swapchain 自己拥有，不能手动销毁
        mSwapImages.clear();

        if (mSwapchain != VK_NULL_HANDLE) {
            VK.vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
            mSwapchain = VK_NULL_HANDLE;
        }
    }

    // ————————————————————————— 每帧资源 —————————————————————————

    VEResult VEVulkanVideoRenderer::ensurePlaneResources(int width, int height) {
        if (mPlanes[0].width == width && mPlanes[0].height == height &&
            mPlanes[0].image != VK_NULL_HANDLE) {
            return VE_OK;
        }
        ALOGI("VEVulkanVideoRenderer: (re)create plane resources for %dx%d", width, height);

        // 尺寸变了要重建，但旧资源可能还被在飞的命令引用
        VK.vkDeviceWaitIdle(mDevice);
        destroyPlaneResources();

        const int chromaW = (width + 1) / 2;
        const int chromaH = (height + 1) / 2;
        const int dims[3][2] = {{width,   height},
                                {chromaW, chromaH},
                                {chromaW, chromaH}};

        for (int i = 0; i < 3; ++i) {
            Plane &p = mPlanes[i];
            p.width = dims[i][0];
            p.height = dims[i][1];
            p.initialized = false;

            VkImageCreateInfo ii{};
            ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ii.imageType = VK_IMAGE_TYPE_2D;
            // 单通道 8 位：与 GLES 侧的 GL_R8 对应，采样后取 .r
            ii.format = VK_FORMAT_R8_UNORM;
            ii.extent.width = static_cast<uint32_t>(p.width);
            ii.extent.height = static_cast<uint32_t>(p.height);
            ii.extent.depth = 1;
            ii.mipLevels = 1;
            ii.arrayLayers = 1;
            ii.samples = VK_SAMPLE_COUNT_1_BIT;
            ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VK_CHECK(VK.vkCreateImage(mDevice, &ii, nullptr, &p.image), "vkCreateImage(plane)");

            VkMemoryRequirements req{};
            VK.vkGetImageMemoryRequirements(mDevice, p.image, &req);
            uint32_t typeIndex = 0;
            if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                &typeIndex)) {
                ALOGE("VEVulkanVideoRenderer: no device-local memory for plane %d", i);
                return VE_NO_MEMORY;
            }
            VkMemoryAllocateInfo mai{};
            mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mai.allocationSize = req.size;
            mai.memoryTypeIndex = typeIndex;
            VK_CHECK(VK.vkAllocateMemory(mDevice, &mai, nullptr, &p.memory),
                     "vkAllocateMemory(plane)");
            VK_CHECK(VK.vkBindImageMemory(mDevice, p.image, p.memory, 0),
                     "vkBindImageMemory(plane)");

            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = p.image;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R8_UNORM;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            VK_CHECK(VK.vkCreateImageView(mDevice, &vi, nullptr, &p.view),
                     "vkCreateImageView(plane)");

            // staging buffer 常驻并持续映射：每帧重新分配/映射会把上传开销
            // 放大好几倍，那才是真正让 Vulkan 比 GLES 慢的原因
            p.stagingSize = static_cast<VkDeviceSize>(p.width) * p.height;
            VEResult ret = createBuffer(p.stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        &p.staging, &p.stagingMemory);
            if (ret != VE_OK) { return ret; }
            VK_CHECK(VK.vkMapMemory(mDevice, p.stagingMemory, 0, p.stagingSize, 0,
                                    &p.stagingMapped), "vkMapMemory(staging)");
        }

        // 三个 view 都建好了才能更新描述符集
        VkDescriptorImageInfo infos[3]{};
        VkWriteDescriptorSet writes[3]{};
        for (int i = 0; i < 3; ++i) {
            infos[i].sampler = mSampler;
            infos[i].imageView = mPlanes[i].view;
            infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = mDescSet;
            writes[i].dstBinding = static_cast<uint32_t>(i);
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &infos[i];
        }
        VK.vkUpdateDescriptorSets(mDevice, 3, writes, 0, nullptr);
        return VE_OK;
    }

    void VEVulkanVideoRenderer::destroyPlaneResources() {
        if (mDevice == VK_NULL_HANDLE) {
            return;
        }
        for (Plane &p : mPlanes) {
            if (p.stagingMapped != nullptr) {
                VK.vkUnmapMemory(mDevice, p.stagingMemory);
                p.stagingMapped = nullptr;
            }
            if (p.staging != VK_NULL_HANDLE) {
                VK.vkDestroyBuffer(mDevice, p.staging, nullptr);
                p.staging = VK_NULL_HANDLE;
            }
            if (p.stagingMemory != VK_NULL_HANDLE) {
                VK.vkFreeMemory(mDevice, p.stagingMemory, nullptr);
                p.stagingMemory = VK_NULL_HANDLE;
            }
            if (p.view != VK_NULL_HANDLE) {
                VK.vkDestroyImageView(mDevice, p.view, nullptr);
                p.view = VK_NULL_HANDLE;
            }
            if (p.image != VK_NULL_HANDLE) {
                VK.vkDestroyImage(mDevice, p.image, nullptr);
                p.image = VK_NULL_HANDLE;
            }
            if (p.memory != VK_NULL_HANDLE) {
                VK.vkFreeMemory(mDevice, p.memory, nullptr);
                p.memory = VK_NULL_HANDLE;
            }
            p.width = 0;
            p.height = 0;
            p.stagingSize = 0;
            p.initialized = false;
        }
    }

    void VEVulkanVideoRenderer::uploadPlane(int index, const uint8_t *src, int linesize,
                                            int w, int h) {
        Plane &p = mPlanes[index];
        if (p.stagingMapped == nullptr || src == nullptr) {
            return;
        }
        uint8_t *dst = static_cast<uint8_t *>(p.stagingMapped);
        if (linesize == w) {
            // 紧凑排布，一次拷完
            memcpy(dst, src, static_cast<size_t>(w) * h);
        } else {
            // AVFrame 的 linesize 通常按 32/64 字节对齐，比实际宽度大，
            // 必须逐行搬，否则画面会整体斜切
            for (int y = 0; y < h; ++y) {
                memcpy(dst + static_cast<size_t>(y) * w,
                       src + static_cast<size_t>(y) * linesize,
                       static_cast<size_t>(w));
            }
        }
    }

    VEResult VEVulkanVideoRenderer::ensureVertexBuffer() {
        if (mVertexBuffer != VK_NULL_HANDLE) {
            return VE_OK;
        }
        const VkDeviceSize size = 16 * sizeof(float);   // 4 顶点 × (vec2 + vec2)
        VEResult ret = createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    &mVertexBuffer, &mVertexMemory);
        if (ret != VE_OK) { return ret; }
        VK_CHECK(VK.vkMapMemory(mDevice, mVertexMemory, 0, size, 0, &mVertexMapped),
                 "vkMapMemory(vertex)");
        return VE_OK;
    }

    void VEVulkanVideoRenderer::updateVertexBuffer() {
        if (mVertexMapped == nullptr) {
            return;
        }
        // 画幅、视口、旋转都没变就不必重算
        if (mVertexFrameW == mFrameWidth && mVertexFrameH == mFrameHeight &&
            mVertexViewW == mViewWidth && mVertexViewH == mViewHeight &&
            mVertexRotation == mRotationDegrees) {
            return;
        }
        mVertexFrameW = mFrameWidth;
        mVertexFrameH = mFrameHeight;
        mVertexViewW = mViewWidth;
        mVertexViewH = mViewHeight;
        mVertexRotation = mRotationDegrees;

        // fit-inside：按宽高比大小比较，不能按帧的横竖朝向判断，
        // 否则横屏视频在竖屏上会放大裁掉大半画面
        float scaleX = 1.0f, scaleY = 1.0f;
        const int viewW = mSwapExtent.width > 0 ? static_cast<int>(mSwapExtent.width) : mViewWidth;
        const int viewH = mSwapExtent.height > 0 ? static_cast<int>(mSwapExtent.height) : mViewHeight;
        if (viewW > 0 && viewH > 0 && mFrameWidth > 0 && mFrameHeight > 0) {
            // 旋转 90/270 后画幅长短边互换，宽高比要按旋转后的算
            const bool swapped = (mRotationDegrees == 90 || mRotationDegrees == 270);
            const int dispW = swapped ? mFrameHeight : mFrameWidth;
            const int dispH = swapped ? mFrameWidth : mFrameHeight;
            const float screenAspect = (float) viewW / (float) viewH;
            const float imageAspect = (float) dispW / (float) dispH;
            if (imageAspect > screenAspect) {
                scaleY = screenAspect / imageAspect;   // 画面比屏幕宽：上下留黑边
            } else {
                scaleX = imageAspect / screenAspect;   // 画面比屏幕窄：左右留黑边
            }
        }

        // 旋转靠重排纹理坐标：那是无量纲空间，转多少度都不形变。裁剪空间
        // 不等比(视口通常不是正方形)，在里面转 90° 会按宽高比拉变形。
        //
        // 与 GLES 版的唯一差别是这里**不需要 Y 翻转**：Vulkan 裁剪空间的
        // Y 轴朝下，(-1,-1) 本身就是屏幕左上角，正好对上纹理原点；GLES 的
        // Y 轴朝上，才要额外乘一个 scale(1,-1,1)。
        static const float kTexCoords[4][8] = {
                /*   0° */ {0, 0,  1, 0,  0, 1,  1, 1},
                /*  90° */ {0, 1,  0, 0,  1, 1,  1, 0},
                /* 180° */ {1, 1,  0, 1,  1, 0,  0, 0},
                /* 270° */ {1, 0,  1, 1,  0, 0,  0, 1},
        };
        int rotIndex = ((mRotationDegrees % 360) + 360) % 360 / 90;
        if (rotIndex < 0 || rotIndex > 3) {
            rotIndex = 0;
        }
        const float *tex = kTexCoords[rotIndex];

        // TRIANGLE_STRIP 顺序：左上 / 右上 / 左下 / 右下
        const float vertices[16] = {
                -scaleX, -scaleY, tex[0], tex[1],
                 scaleX, -scaleY, tex[2], tex[3],
                -scaleX,  scaleY, tex[4], tex[5],
                 scaleX,  scaleY, tex[6], tex[7],
        };
        memcpy(mVertexMapped, vertices, sizeof(vertices));
        ALOGI("VEVulkanVideoRenderer: vertices updated, frame %dx%d view %dx%d rot %d "
              "scale %.3f/%.3f", mFrameWidth, mFrameHeight, viewW, viewH,
              mRotationDegrees, scaleX, scaleY);
    }

    void VEVulkanVideoRenderer::updateColorParams(const std::shared_ptr<VEFrame> &frame) {
        AVFrame *av = frame->getFrame();

        // 与 GLES 侧同一套判定：YUVJ* 是 full range 的历史表达；
        // 否则以 color_range 为准，未标注时按视频常态假定 limited range
        const bool fullRange =
                av->color_range == AVCOL_RANGE_JPEG ||
                av->format == AV_PIX_FMT_YUVJ420P;

        // 未标注 colorspace 时按分辨率推断：HD 及以上用 BT.709，否则 BT.601
        int space = av->colorspace;
        if (space == AVCOL_SPC_UNSPECIFIED) {
            space = (av->height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }
        const bool bt709 = (space == AVCOL_SPC_BT709);

        const int rangeKey = fullRange ? 1 : 0;
        if (mLastColorRange == rangeKey && mLastColorSpace == space) {
            return;
        }
        mLastColorRange = rangeKey;
        mLastColorSpace = space;

        // yScale: limited range 的 Y 落在 [16,235]，要展开回 [0,1]
        const float yScale = fullRange ? 1.0f : (255.0f / 219.0f);
        // 色度系数：limited range 的 UV 落在 [16,240]
        const float cScale = fullRange ? 1.0f : (255.0f / 224.0f);
        const float kr = bt709 ? 0.2126f : 0.299f;
        const float kb = bt709 ? 0.0722f : 0.114f;
        const float kg = 1.0f - kr - kb;

        const float vToR = cScale * 2.0f * (1.0f - kr);
        const float uToB = cScale * 2.0f * (1.0f - kb);
        const float vToG = -cScale * 2.0f * (1.0f - kr) * kr / kg;
        const float uToG = -cScale * 2.0f * (1.0f - kb) * kb / kg;

        // push constant 里的 mat3 按列存，且每列占 16 字节(std140 对齐)，
        // 所以第 4 个 float 是填充位，不能省——省了整个矩阵会错位一列
        std::memset(&mColorParams, 0, sizeof(mColorParams));
        float *m = mColorParams.colorMat;
        m[0] = yScale; m[1] = yScale; m[2] = yScale;   // col0: Y → R,G,B
        m[4] = 0.0f;   m[5] = uToG;   m[6] = uToB;     // col1: U → R,G,B
        m[8] = vToR;   m[9] = vToG;   m[10] = 0.0f;    // col2: V → R,G,B

        mColorParams.colorOffset[0] = fullRange ? 0.0f : (16.0f / 255.0f);
        mColorParams.colorOffset[1] = 128.0f / 255.0f;
        mColorParams.colorOffset[2] = 128.0f / 255.0f;

        ALOGI("VEVulkanVideoRenderer color conversion: %s range, %s",
              fullRange ? "full" : "limited", bt709 ? "BT.709" : "BT.601");
    }

    // ————————————————————————— 渲染 —————————————————————————

    VEResult VEVulkanVideoRenderer::renderFrame(const std::shared_ptr<VEFrame> &frame) {
        if (!mCoreReady || !mSwapchainReady) {
            // surface detach 期间丢帧是正常的，别刷日志
            return VE_OK;
        }
        if (frame == nullptr || frame->getFrame() == nullptr) {
            return VE_INVALID_PARAMS;
        }
        AVFrame *av = frame->getFrame();
        if (av->width <= 0 || av->height <= 0) {
            ALOGE("VEVulkanVideoRenderer::renderFrame invalid frame size %dx%d",
                  av->width, av->height);
            return VE_INVALID_PARAMS;
        }

        mFrameWidth = av->width;
        mFrameHeight = av->height;

        VEResult ret = ensurePlaneResources(av->width, av->height);
        if (ret != VE_OK) { return ret; }
        updateColorParams(frame);
        updateVertexBuffer();

        // 等上一帧彻底交付：既是给 swapchain 让位，也保证下面往 staging
        // buffer 里 memcpy 时 GPU 已经读完了上一帧的内容
        VK.vkWaitForFences(mDevice, 1, &mInFlight, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acquired = VK.vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX,
                                                     mImageAvailable, VK_NULL_HANDLE,
                                                     &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            // 窗口尺寸变了。这里只能重建，且重建后本帧直接丢弃——
            // mImageAvailable 没有被 signal，继续往下提交会死等
            ALOGI("VEVulkanVideoRenderer: swapchain out of date, recreating");
            return changeSurface(mWin, mViewWidth, mViewHeight);
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            ALOGE("VEVulkanVideoRenderer: vkAcquireNextImageKHR failed, VkResult=%d", acquired);
            return VE_UNKNOWN_ERROR;
        }

        // 三个平面先搬进 staging buffer(CPU 侧)
        const int chromaW = (av->width + 1) / 2;
        const int chromaH = (av->height + 1) / 2;
        uploadPlane(0, av->data[0], av->linesize[0], av->width, av->height);
        uploadPlane(1, av->data[1], av->linesize[1], chromaW, chromaH);
        uploadPlane(2, av->data[2], av->linesize[2], chromaW, chromaH);

        VK.vkResetFences(mDevice, 1, &mInFlight);
        VK.vkResetCommandBuffer(mCmdBuffer, 0);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(VK.vkBeginCommandBuffer(mCmdBuffer, &bi), "vkBeginCommandBuffer");

        // 上传与绘制录进同一个命令缓冲：分两次提交会多一次 GPU 往返，
        // 在 60fps 下这点开销是能测出来的
        for (int i = 0; i < 3; ++i) {
            Plane &p = mPlanes[i];
            transitionLayout(mCmdBuffer, p.image,
                             p.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            // staging 里是紧凑排布(uploadPlane 已经把 linesize 抹平了)
            copy.bufferRowLength = static_cast<uint32_t>(p.width);
            copy.bufferImageHeight = static_cast<uint32_t>(p.height);
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent.width = static_cast<uint32_t>(p.width);
            copy.imageExtent.height = static_cast<uint32_t>(p.height);
            copy.imageExtent.depth = 1;
            VK.vkCmdCopyBufferToImage(mCmdBuffer, p.staging, p.image,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            transitionLayout(mCmdBuffer, p.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            p.initialized = true;
        }

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};   // fit-inside 的黑边

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = mRenderPass;
        rp.framebuffer = mFramebuffers[imageIndex];
        rp.renderArea.extent = mSwapExtent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        VK.vkCmdBeginRenderPass(mCmdBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VK.vkCmdBindPipeline(mCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
        VK.vkCmdBindDescriptorSets(mCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   mPipelineLayout, 0, 1, &mDescSet, 0, nullptr);
        VK.vkCmdPushConstants(mCmdBuffer, mPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(ColorParams), &mColorParams);
        VkDeviceSize offset = 0;
        VK.vkCmdBindVertexBuffers(mCmdBuffer, 0, 1, &mVertexBuffer, &offset);
        VK.vkCmdDraw(mCmdBuffer, 4, 1, 0, 0);

        VK.vkCmdEndRenderPass(mCmdBuffer);
        VK_CHECK(VK.vkEndCommandBuffer(mCmdBuffer), "vkEndCommandBuffer");

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &mImageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &mCmdBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &mRenderFinished;
        VK_CHECK(VK.vkQueueSubmit(mQueue, 1, &submit, mInFlight), "vkQueueSubmit");

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &mRenderFinished;
        present.swapchainCount = 1;
        present.pSwapchains = &mSwapchain;
        present.pImageIndices = &imageIndex;
        VkResult presented = VK.vkQueuePresentKHR(mQueue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            ALOGI("VEVulkanVideoRenderer: present reports %d, recreating swapchain", presented);
            return changeSurface(mWin, mViewWidth, mViewHeight);
        }
        if (presented != VK_SUCCESS) {
            ALOGE("VEVulkanVideoRenderer: vkQueuePresentKHR failed, VkResult=%d", presented);
            return VE_UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    // ————————————————————————— 小工具 —————————————————————————

    bool VEVulkanVideoRenderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props,
                                               uint32_t *outIndex) const {
        VkPhysicalDeviceMemoryProperties memProps{};
        VK.vkGetPhysicalDeviceMemoryProperties(mPhysical, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) == 0) { continue; }
            if ((memProps.memoryTypes[i].propertyFlags & props) != props) { continue; }
            *outIndex = i;
            return true;
        }
        return false;
    }

    VEResult VEVulkanVideoRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                 VkMemoryPropertyFlags props,
                                                 VkBuffer *outBuffer,
                                                 VkDeviceMemory *outMemory) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(VK.vkCreateBuffer(mDevice, &bi, nullptr, outBuffer), "vkCreateBuffer");

        VkMemoryRequirements req{};
        VK.vkGetBufferMemoryRequirements(mDevice, *outBuffer, &req);
        uint32_t typeIndex = 0;
        if (!findMemoryType(req.memoryTypeBits, props, &typeIndex)) {
            VK.vkDestroyBuffer(mDevice, *outBuffer, nullptr);
            *outBuffer = VK_NULL_HANDLE;
            ALOGE("VEVulkanVideoRenderer: no memory type for buffer (props 0x%x)", props);
            return VE_NO_MEMORY;
        }
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = typeIndex;
        VK_CHECK(VK.vkAllocateMemory(mDevice, &mai, nullptr, outMemory), "vkAllocateMemory");
        VK_CHECK(VK.vkBindBufferMemory(mDevice, *outBuffer, *outMemory, 0),
                 "vkBindBufferMemory");
        return VE_OK;
    }

    void VEVulkanVideoRenderer::transitionLayout(VkCommandBuffer cmd, VkImage image,
                                                 VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage;
        VkPipelineStageFlags dstStage;
        if (to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            if (from == VK_IMAGE_LAYOUT_UNDEFINED) {
                // 首帧：图像里还没有需要保留的内容
                barrier.srcAccessMask = 0;
                srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            } else {
                // 后续帧：要等片元着色器读完上一帧才能覆写
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        VK.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    VkCommandBuffer VEVulkanVideoRenderer::beginOneShot() {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = mCmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (VK.vkAllocateCommandBuffers(mDevice, &ai, &cmd) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK.vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void VEVulkanVideoRenderer::endOneShot(VkCommandBuffer cmd) {
        if (cmd == VK_NULL_HANDLE) {
            return;
        }
        VK.vkEndCommandBuffer(cmd);
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        VK.vkQueueSubmit(mQueue, 1, &submit, VK_NULL_HANDLE);
        VK.vkQueueWaitIdle(mQueue);
        VK.vkFreeCommandBuffers(mDevice, mCmdPool, 1, &cmd);
    }

} // namespace VE
