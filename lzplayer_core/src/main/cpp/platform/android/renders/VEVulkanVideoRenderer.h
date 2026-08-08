#ifndef LZPLAYER_VEVULKANVIDEORENDERER_H
#define LZPLAYER_VEVULKANVIDEORENDERER_H

#include <vector>

// vulkan.h 只能经 VEVulkanApi.h 引入：VK_USE_PLATFORM_ANDROID_KHR 必须在
// vulkan.h 之前定义，否则 vulkan_android.h(vkCreateAndroidSurfaceKHR 等)
// 不会被拉进来，而头文件守卫会让后面再补定义也无效
#include "VEVulkanApi.h"
#include "IVideoRender.h"

namespace VE {

    /// Vulkan 视频渲染器：IVideoRender 的第二个实现，与 VEGLESVideoRenderer 平级。
    ///
    /// **适用范围有限，别误判它的价值**：它只在软解路径生效。硬解走 MediaCodec
    /// 直出 Surface，既不经过 GLES 也不经过 Vulkan。软解的瓶颈在解码而非纹理
    /// 上传，所以这里换 Vulkan 不会让软解变快。它存在的意义是让"渲染后端可插拔"
    /// 这个扩展点真的有两个实现，并为将来的滤镜/特效(Vulkan 计算管线)铺路。
    ///
    /// 渲染路径与 GLES 侧一致：Y/U/V 三个单平面 R8 图像，经 staging buffer 上传，
    /// 片元着色器里做 YUV→RGB；色彩系数与量程偏移按帧参数经 push constant 下发；
    /// 旋转靠重排纹理坐标(裁剪空间非等比，在其中旋转会按视口宽高比形变)。
    ///
    /// libvulkan.so 随 Android 7.0(API 24)引入，与本工程 minSdk 一致，因此直接
    /// 链接即可，不需要像 libaaudio 那样 dlopen。但设备/驱动差异大，初始化失败
    /// 是常态而非异常——失败一律返回错误，由工厂回退到 GLES。
    class VEVulkanVideoRenderer : public IVideoRender {
    public:
        VEVulkanVideoRenderer();
        ~VEVulkanVideoRenderer() override;

        VEResult initialize(VEBundle params) override;
        VEResult changeSurface(ANativeWindow *win, int viewWidth, int viewHeight) override;
        VEResult renderFrame(const std::shared_ptr<VEFrame> &frame) override;
        VEResult uninitialize() override;

        /// 运行期能否用 Vulkan。工厂建对象前先问一句，省得白走一遍初始化。
        static bool isAvailable();

    private:
        // —— 与 surface 无关的一次性资源 ——
        VEResult createInstance();
        VEResult pickPhysicalDeviceAndQueue();
        VEResult createDevice();
        VEResult createSamplersAndDescriptorLayout();
        VEResult createCommandPool();
        VEResult createSyncObjects();

        // —— 跟着 surface / 尺寸变化重建的资源 ——
        VEResult createSurface(ANativeWindow *win);
        VEResult createSwapchain();
        VEResult createRenderPass();
        VEResult createPipeline();
        VEResult createFramebuffers();
        void destroySwapchainResources();

        // —— 每帧数据 ——
        /// 按帧尺寸(重)建 Y/U/V 三个平面图像与 staging buffer
        VEResult ensurePlaneResources(int width, int height);
        void destroyPlaneResources();
        /// 把一个平面从 CPU 拷进 staging buffer 再 copy 到 image
        void uploadPlane(int index, const uint8_t *src, int linesize, int w, int h);
        /// 顶点缓冲：位置(含 fit-inside)+ 纹理坐标(含旋转)
        VEResult ensureVertexBuffer();
        void updateVertexBuffer();
        /// 按帧的 color_range/colorspace 更新 push constant 内容
        void updateColorParams(const std::shared_ptr<VEFrame> &frame);

        // —— 通用小工具 ——
        VEResult createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer *outBuffer, VkDeviceMemory *outMemory);
        bool findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props,
                            uint32_t *outIndex) const;
        /// 借一个一次性命令缓冲同步执行(上传/布局转换用，帧率无关的低频操作)
        VkCommandBuffer beginOneShot();
        void endOneShot(VkCommandBuffer cmd);
        void transitionLayout(VkCommandBuffer cmd, VkImage image,
                              VkImageLayout from, VkImageLayout to);

        /// push constant 布局必须与 shader 里的 ColorParams 对齐：
        /// std140 下 mat3 占 3 个 vec4，所以按列存进 3 组 4 float
        struct ColorParams {
            float colorMat[12];     ///< 3 列 × (vec3 + 1 padding)
            float colorOffset[4];   ///< vec3 + 1 padding
        };

        struct Plane {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkBuffer staging = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            void *stagingMapped = nullptr;
            VkDeviceSize stagingSize = 0;
            int width = 0;
            int height = 0;
            /// 首次上传前图像还处于 UNDEFINED，需要一次布局转换
            bool initialized = false;
        };

        VkInstance mInstance = VK_NULL_HANDLE;
        VkPhysicalDevice mPhysical = VK_NULL_HANDLE;
        VkDevice mDevice = VK_NULL_HANDLE;
        VkQueue mQueue = VK_NULL_HANDLE;
        uint32_t mQueueFamily = 0;

        VkSurfaceKHR mSurface = VK_NULL_HANDLE;
        VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
        VkFormat mSwapFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D mSwapExtent{0, 0};
        std::vector<VkImage> mSwapImages;
        std::vector<VkImageView> mSwapViews;
        std::vector<VkFramebuffer> mFramebuffers;

        VkRenderPass mRenderPass = VK_NULL_HANDLE;
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
        VkPipeline mPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout mDescLayout = VK_NULL_HANDLE;
        VkDescriptorPool mDescPool = VK_NULL_HANDLE;
        VkDescriptorSet mDescSet = VK_NULL_HANDLE;
        VkSampler mSampler = VK_NULL_HANDLE;

        VkCommandPool mCmdPool = VK_NULL_HANDLE;
        VkCommandBuffer mCmdBuffer = VK_NULL_HANDLE;
        VkSemaphore mImageAvailable = VK_NULL_HANDLE;
        VkSemaphore mRenderFinished = VK_NULL_HANDLE;
        VkFence mInFlight = VK_NULL_HANDLE;

        Plane mPlanes[3];
        VkBuffer mVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory mVertexMemory = VK_NULL_HANDLE;
        void *mVertexMapped = nullptr;

        ANativeWindow *mWin = nullptr;
        int mViewWidth = 0;
        int mViewHeight = 0;
        int mFrameWidth = 0;
        int mFrameHeight = 0;
        int mRotationDegrees = 0;
        /// 顶点数据只在画幅/旋转变化时重算
        int mVertexFrameW = 0;
        int mVertexFrameH = 0;
        int mVertexViewW = 0;
        int mVertexViewH = 0;
        int mVertexRotation = -1;

        ColorParams mColorParams{};
        int mLastColorRange = -1;
        int mLastColorSpace = -1;

        bool mCoreReady = false;      ///< instance/device 等一次性资源就绪
        bool mSwapchainReady = false; ///< 与 surface 绑定的资源就绪
    };
}

#endif //LZPLAYER_VEVULKANVIDEORENDERER_H
