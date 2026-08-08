# vulkan-renderer 设计文档

> 创建日期: 2026-08-08
> 关联 feature: [../features/vulkan-renderer/](../features/vulkan-renderer/)

## 背景与目标

### 需求拆解：一句话里的两件事，其中一件已经存在

用户原话是"音频播放也要支持 opensles/vulkan 渲染"。拆开来看是两件独立的事，**其中音频那件已经做完了**：

1. **OpenSL ES 音频后端 —— 已实现且已在真机验证通过，本 feature 不含任何音频工作。**
   - `VEAudioRender` 已按系统版本选择后端：API 26+ 用 AAudio（`VEAAudioRender`，运行期 dlopen/dlsym），否则用 SLES（`VEAudioSLESRender`）。
   - 诊断面板已有"强制 OpenSL ES"开关，可在两个后端之间**双向**切换（下次 prepare 生效）。
   - 本次真机实测结论：切到 SLES 后，进度 10 秒实际推进 9.88 秒（比例 1.0，时钟正常）；静音问题仅在起播首秒出现一次，之后不复现。
   - 因此本文档只是**记录这项既有能力与验证结果**，不再派生实施步骤。

2. **Vulkan 视频渲染 —— 新工作，是本 feature 的全部内容。**
   - 目标：新增 `VEVulkanVideoRenderer`，与既有 `VEGLESVideoRenderer` 平级实现 `IVideoRender`，由工厂按策略选择，默认仍走 GLES。

### 收益边界（重要：避免后人误判本 feature 的价值）

**Vulkan 渲染器只影响软解路径。** 硬解走 MediaCodec 直出 Surface，既不经过 GLES 也不会经过 Vulkan。而软解路径的瓶颈是**解码**而不是纹理上传，所以本 feature 的价值**不在性能**，而在于：

- (a) **兑现"渲染后端可插拔"这个扩展维度**：让 `IVideoRender` 真正拥有两个实现，接口抽象从"纸面设计"变成"被验证过的设计"。
- (b) **为未来视频滤镜/特效铺路**：Vulkan 计算管线在多 pass 滤镜、计算着色器场景优于 GLES。

不要拿本 feature 去做性能宣称，也不要期待硬解路径有任何变化。

## 技术前提（已核实）

| 前提 | 结论 |
|------|------|
| SPIR-V 编译工具 | NDK 自带 glslc：`/Users/lizhen/Library/Android/sdk/ndk/25.1.8937393/shader-tools/darwin-x86_64/glslc`，可把 GLSL 编成 SPIR-V，无需自行 build shaderc |
| libvulkan 可用性 | `libvulkan.so` 存在于 API 24 的 sysroot，理论上可直接链接（Vulkan 随 Android 7.0/API 24 引入，与本工程 minSdk 24 一致）。**但本 feature 最终没有直接链接**，见下方"链接策略（方案修订）" |

### 链接策略（方案修订，2026-08-08 步骤1 实施时确定）

原方案写的是"直接进 `target_link_libraries`"。**实施时改成了运行期动态加载**：

- `dlopen("libvulkan.so")` → `vkGetInstanceProcAddr` → 派生全部约 70 个入口，集中在新增的 `VEVulkanApi.{h,cpp}`（X-macro 函数表）里。
- `CMakeLists.txt` **不加** `vulkan` 到 `target_link_libraries`。

**为什么改**：直接链接会让 `libvulkan.so` 成为 `liblzplayer_core.so` 的 DT_NEEDED 硬依赖。一旦某台设备上它缺失或加载失败，后果不是"退回 GLES"，而是**整个 native 库装不进来、播放器根本起不来**。渲染后端本就设计成可回退（见风险 1），没有理由为一个可回退的后端把整个库押上去。这正是 `libaaudio` 已经踩过的坑——当时的处理方式（dlopen/dlsym）在这里同样适用。

已用 `llvm-readelf -d` 验证产物：NEEDED 列表里既没有 `libvulkan`，也没有 `libaaudio`。

**强制约束：所有 Vulkan 调用必须走 `VK.xxx(...)` 函数表，不得直写 `vkXxx(...)`。**
直写会让链接器重新引入 `libvulkan` 的符号依赖，把上面这层保护整个抵消掉。新增/修改 Vulkan 代码时按此检查（可用 `llvm-readelf -d` 复核 NEEDED 列表作为兜底）。

**头文件约束：`vulkan.h` 只能经 `VEVulkanApi.h` 引入。**
`VK_USE_PLATFORM_ANDROID_KHR` 必须在 `vulkan.h` 之前定义，这个宏定义在 `VEVulkanApi.h` 里。实施时 `VEVulkanVideoRenderer.h` 曾自行 include `vulkan.h`，结果 `vkCreateAndroidSurfaceKHR` 等 Android 扩展符号全部找不到。

## 技术方案

### 1. 渲染器实现

新增 `VEVulkanVideoRenderer`，实现既有 `IVideoRender` 接口（`interface/IVideoRender.h`）的四个方法，与 `VEGLESVideoRenderer` 平级：

- `initialize(VEBundle params)` —— 建 instance / physical device / logical device / queue，读出 `rotation` 等参数（与 GLES 侧一致的 params 约定）
- `changeSurface(ANativeWindow *win, int viewWidth, int viewHeight)` —— surface 与 swapchain 的建立/重建/释放
- `renderFrame(const std::shared_ptr<VEFrame> &frame)` —— 上传 YUV、录制并提交命令、present
- `uninitialize()` —— 逆序释放

### 2. 工厂与策略开关

新增 `VEVideoRenderFactory`：按策略返回 GLES 或 Vulkan 实例。

- **默认 GLES**（成熟稳定，已修过画幅/色彩量程/旋转多个问题）；Vulkan 仅在策略开关打开时启用。
- 策略沿用现成的 force-flag 链路，与 `setForceSoftwareDecoder` / `setForceSlesAudio` 完全同构：
  `VEPlayer`（原子 flag，prepare 时读取）→ `VEPlayerDriver` → `native_PlayerInterface.cpp` → `NativeLib.java` → `VEPlayer.java`。
- 诊断面板（`app/src/main/java/com/example/lzplayer/console/DiagnosticsSheet.kt`）加**第三个开关**："用 Vulkan 渲染（下次 prepare 生效）"，措辞与既有两个开关一致。
- 当前渲染后端要在诊断读数里可见（GLES / Vulkan），**回退到 GLES 时必须如实显示 GLES**，不能显示成 Vulkan。

替换点：`VEVideoDisplay.cpp` 现在两处直接 `std::make_shared<VEGLESVideoRenderer>()`（约 233 行与 457 行），改为经工厂创建。

#### 2.1 策略开关的命令行入口（步骤6 实施时补充）

只能手点诊断面板的话，回归脚本无法自动化，理由与当初引入 `EXTRA_SOURCE` 相同。因此 `ConsoleActivity` 增加两个 intent extra：

- `EXTRA_FORCE_SOFTWARE = "software"`（boolean）—— 强制软解
- `EXTRA_PREFER_VULKAN = "vulkan"`（boolean）—— 使用 Vulkan 渲染

```
adb shell am start -n com.example.lzplayer/.console.ConsoleActivity \
  -e source <path> --ez autoplay true --ez software true --ez vulkan true
```

两个策略与 `autoplay`/`source` 一样，在 `openSource` 新建播放器时统一重新下发。

### 3. YUV 上传路径（与 GLES 对齐）

- **3 个单平面 R8 图像**（`VK_FORMAT_R8_UNORM`）分别承载 Y/U/V，与 GLES 侧三张 `GL_LUMINANCE`/R8 纹理一一对应。
- **staging buffer 上传**：host-visible staging buffer → `vkCmdCopyBufferToImage` → device-local image；**staging buffer 必须跨帧复用**，尺寸不变就不重新分配。
- 采样器：线性过滤 + clamp to edge。
- 描述符集：3 个 `combined image sampler`（set 0，binding 0/1/2），与 `shaders/yuv.frag` 已写好的 layout 一致。

### 4. 色彩转换（复用 GLES 已修好的判定）

YUV→RGB 的**系数矩阵与量程偏移由 CPU 侧算好，经 push constant 传入**（`mat3 colorMat` + `vec3 colorOffset`），不在 shader 里做分支。

判定逻辑直接沿用 `VEGLESVideoRenderer` 已修正过的那一套：

- **量程**：`YUVJ*` 像素格式视为 full range；否则以 `color_range` 为准；未标注时按视频常态假定 **limited range**。
- **色彩空间**：未标注 `colorspace` 时按分辨率推断，`height >= 720` 用 **BT.709**，否则 **BT.601**（SMPTE170M）。
- limited range 展开：Y 用 `255/219`，UV 用 `255/224`，偏移 `16/255`。

这套判定是 GLES 侧踩过坑修好的（不做展开会导致黑位停在 6% 灰、整体对比度偏低），Vulkan 侧**必须复用同一判定**，两条路径的输出画面应当逐像素一致（允许滤波差异内的微小偏差）。

### 5. 旋转

**靠重排纹理坐标实现**，与 GLES 侧一致。不在裁剪空间做旋转 —— 那样会在非等比视口下产生形变。

**与 GLES 的一处坐标系差异**：Vulkan 裁剪空间 Y 轴朝下，`(-1,-1)` 即屏幕左上角。因此 GLES 侧那个 `scale(1,-1,1)` 翻转矩阵在 Vulkan 侧**不需要**，纹理坐标表可直接复用。误加翻转会得到上下颠倒的画面。

### 5.1 错误码约定

`utils/VEError.h` 里**没有** `VE_NOT_SUPPORT`。Vulkan 侧的"不支持/环境不满足"一律返回 `VE_INVALID_OPERATION`。

### 6. Shader 与 SPIR-V 内嵌

- 源文件：`platform/android/renders/shaders/yuv.vert`、`yuv.frag`（GLSL 450）。
- 用 NDK glslc 预编译成 SPIR-V，再转成 `uint32_t` 数组内嵌进 `VEVulkanShaders.h`（`VE::vkshaders::kYuvVert` / `kYuvFrag`）。
- **运行期不需要任何 shader 编译器**。改 shader 后需重新生成头文件，重新生成命令写在头文件注释里。

### 7. Swapchain 生命周期

- surface 变化或尺寸变化时**重建 swapchain**（含 image views、framebuffers；render pass 与 pipeline 在格式不变时可保留）。
- **surface 置空时释放 swapchain 但保留 device**：避免每次切前后台/旋转都把整个 Vulkan 环境推倒重来。
- `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` 走同一条重建路径。

### 8. 每帧同步

- image available semaphore + render finished semaphore + in-flight fence，标准三件套。
- present 之后不阻塞等待，靠 fence 控制帧资源复用（命令缓冲、staging buffer、描述符集按 in-flight 数量成组）。

## 涉及模块与文件

### 新增
- `lzplayer_core/src/main/cpp/platform/android/renders/VEVulkanApi.{h,cpp}`（**方案修订新增**：dlopen + 函数表，`VK_USE_PLATFORM_ANDROID_KHR` 与 `vulkan.h` 的唯一引入点）
- `lzplayer_core/src/main/cpp/platform/android/renders/VEVulkanVideoRenderer.{h,cpp}`
- `lzplayer_core/src/main/cpp/platform/android/renders/VEVulkanShaders.h`
- `lzplayer_core/src/main/cpp/platform/android/renders/shaders/yuv.vert`、`yuv.frag`
- `lzplayer_core/src/main/cpp/core/VEVideoRenderFactory.{h,cpp}`

### 修改
- `lzplayer_core/src/main/cpp/CMakeLists.txt` —— 加入新增源文件；**不加** `vulkan` 到 `target_link_libraries`（见"链接策略"）
- `lzplayer_core/src/main/cpp/core/VEVideoDisplay.cpp` —— 两处直接 new GLES 改为走工厂
- `lzplayer_core/src/main/cpp/core/VEVideoDisplay.h` —— include 改为 `VEVideoRenderFactory.h`，新增 `setPreferVulkan(bool)` / `renderBackendName()` 与成员 `mRenderPolicy` / `mUsedVulkan`
- `lzplayer_core/src/main/cpp/core/VEPlayer.{h,cpp}`、`VEPlayerDriver.{h,cpp}`、`core/native_PlayerInterface.{h,cpp}`、`core/VEJvmOnLoad.cpp`（JNI 方法表注册 `nativeSetPreferVulkanRender`）
- `lzplayer_core/src/main/java/com/example/lzplayer_core/NativeLib.java`、`VEPlayer.java`
- `app/src/main/java/com/example/lzplayer/console/DiagnosticsSheet.kt`（第三个开关 + 后端读数）
- `app/src/main/java/com/example/lzplayer/console/ConsoleActivity.kt`（`preferVulkan` 状态 + 两个 intent extra，见 2.1）

### 不改动
- 硬解路径（MediaCodec 直出 Surface）—— 与本 feature 无关
- 音频侧任何代码 —— OpenSL ES 已完成并验证

## 风险与依赖

1. **Vulkan 初始化失败面广**（设备/驱动差异、扩展缺失、格式不支持）。
   → 必须像解码器 fallback 那样**支持回退到 GLES**，且**回退要在 UI 上可见**（诊断面板后端读数显示 GLES + 事件流有一条回退事件）。静默回退等于埋雷。

2. **每帧 CPU→GPU 上传三个平面，实现不当会比 GLES 更慢。**
   → staging buffer 必须**跨帧复用**而不是每帧分配；描述符集与命令缓冲同样预分配。若实测明显慢于 GLES，视为本步骤未通过。

3. **本 feature 只在软解路径生效。**
   → 验证时**必须先打开"强制软解"开关**，否则根本走不到渲染器，容易误判成"Vulkan 开关没生效"。这一点已写进步骤 7 的验收条件。

4. **旋转与画幅回归成本**：GLES 侧的画幅/量程/旋转是逐个修出来的，Vulkan 侧等于重走一遍。验收要求三项与 GLES 结果**逐项对照一致**，而不是"看起来正常"。

5. **依赖**：诊断面板（test-console-ui）是本 feature 的操作载体与观测窗口；开关与读数需在其框架内新增。
