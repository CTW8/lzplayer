# vulkan-renderer 实施计划

> 设计文档: ../../docs/vulkan-renderer-design.md
> 创建日期: 2026-08-08

## 方案摘要

新增 `VEVulkanVideoRenderer` 实现既有 `IVideoRender` 接口，与 `VEGLESVideoRenderer` 平级，由新增的 `VEVideoRenderFactory` 按策略选择（**默认 GLES**，Vulkan 由开关启用，策略经 VEPlayer→JNI→Java 透到诊断面板第三个开关）。渲染路径与 GLES 对齐：3 张单平面 R8 图像承载 Y/U/V + 复用的 staging buffer 上传 + 采样器，YUV→RGB 系数与量程偏移经 push constant 传入，沿用 GLES 已修好的 BT.601/709 × full/limited 判定；旋转靠重排纹理坐标。SPIR-V 由 NDK glslc 预编译内嵌成头文件数组，运行期无需 shader 编译器。

**本 feature 不含音频工作**：OpenSL ES 音频后端已实现且真机验证通过（详见设计文档背景段）。**本 feature 只影响软解路径**，价值在于兑现渲染后端可插拔与为未来滤镜铺路，**不是性能优化**。

## 实施步骤

1. **步骤1 shader 与 SPIR-V 内嵌 + Vulkan 动态加载层**（方案已修订，见下）
   - `shaders/yuv.vert`、`shaders/yuv.frag`（GLSL 450，frag 用 3 个 combined image sampler + push constant 色彩参数）。
   - 用 NDK glslc（`$NDK/shader-tools/darwin-x86_64/glslc`）编成 SPIR-V，转 `uint32_t` 数组内嵌到 `VEVulkanShaders.h`，重新生成命令写进头文件注释。
   - ~~`CMakeLists.txt` 的 `target_link_libraries` 加 `vulkan`~~ → **改为**：新增 `VEVulkanApi.{h,cpp}`，`dlopen("libvulkan.so")` + `vkGetInstanceProcAddr` 派生约 70 个入口（X-macro 函数表），CMake **不链接** vulkan。理由与约束见设计文档"链接策略（方案修订）"。
   - 验收（已按修订后的标准通过）：模块完整编译链接通过，`llvm-readelf -d` 确认 `.so` 的 NEEDED 列表**不含** libvulkan / libaaudio。

2. **步骤2 VEVulkanVideoRenderer 骨架**
   - instance（含 `VK_KHR_surface` / `VK_KHR_android_surface`）→ physical device 选择 → logical device + graphics/present queue → 从 `ANativeWindow` 建 surface → swapchain（含 image views）→ render pass → graphics pipeline（用步骤1 的 SPIR-V）→ framebuffers → command pool。
   - `initialize` / `uninitialize` 成对，逆序释放，无泄漏。
   - 验收：软解下调用 initialize/changeSurface 后能清屏出一个纯色帧（此步不要求出画面内容）；uninitialize 后无 Vulkan 校验层报错、反复 init/uninit 不崩。

3. **步骤3 YUV 三平面上传与描述符集**
   - 3 个 `VK_FORMAT_R8_UNORM` device-local 图像 + 采样器；staging buffer 上传并**跨帧复用**（尺寸不变不重分配）。
   - 描述符集 layout 与 frag shader 的 set0/binding0-2 对齐；push constant 传 `mat3 colorMat` + `vec3 colorOffset`，判定逻辑与 GLES 侧共用（YUVJ→full，否则看 color_range，未标注按 limited；未标注 colorspace 按 height>=720 选 BT.709）。
   - 验收：单帧静态画面色彩与 GLES 渲染同一帧目视一致；limited range 素材黑位到位（不是 6% 灰）；Vulkan 校验层无描述符/布局告警。

4. **步骤4 renderFrame 命令录制/提交/present + 同步**
   - 每帧：等 in-flight fence → acquire image → 上传 → 录制并提交 → present；semaphore/fence 三件套；命令缓冲与帧资源按 in-flight 数量预分配。
   - 验收：软解连续播放不卡不撕裂，画面与 GLES 同步流畅；无每帧内存分配（可用简单计数或 log 抽查确认 staging buffer 未重分配）。

5. **步骤5 changeSurface 的 swapchain 重建与 surface 置空处理**
   - surface / 尺寸变化重建 swapchain（image views、framebuffers）；`OUT_OF_DATE_KHR` / `SUBOPTIMAL_KHR` 走同一路径。
   - **surface 置空只释放 swapchain，保留 device**。
   - 验收：旋转屏幕、切后台再回前台、反复置空/设回 surface，播放均不中断不崩，画幅正确。

6. **步骤6 VEVideoRenderFactory + 策略开关全链路**
   - 新增工厂，`VEVideoDisplay.cpp` 两处直接 new GLES（约 233/457 行）改走工厂；默认 GLES。
   - 策略 flag 全链路：`VEPlayer`（原子 flag，prepare 时读）→ `VEPlayerDriver` → `native_PlayerInterface.cpp` → `NativeLib.java` → `VEPlayer.java`，与 `setForceSlesAudio` 同构。
   - `DiagnosticsSheet.kt` 加第三个开关"用 Vulkan 渲染（下次 prepare 生效）"，并在读数里显示当前后端（GLES/Vulkan）。
   - **Vulkan 初始化失败自动回退 GLES，且回退在 UI 上可见**（读数显示 GLES + 事件流一条回退事件）。
   - **（实施时补充）`ConsoleActivity` 增加 intent extra `EXTRA_FORCE_SOFTWARE = "software"` 与 `EXTRA_PREFER_VULKAN = "vulkan"`**，让两个策略开关可脚本化下发，理由同当初的 `EXTRA_SOURCE`（只能手点面板则回归无法自动化）。详见设计文档 2.1。
   - 验收：开关打开重新 prepare 后读数显示 Vulkan；关闭后显示 GLES；人为制造初始化失败时读数显示 GLES 且事件流有回退记录，播放不中断。

7. **步骤7 真机验证**
   - **前置1：必须先确认设备处于解锁亮屏状态** —— `dumpsys power` 的 `mWakefulness=Awake` 且 `dumpsys window` 的 `isKeyguardShowing=false`。锁屏/息屏会导致 Activity surface 被回收（日志表现为 `changeSurface new surface 0x0` → `surface detached` 且不再重建），任何渲染后端都是零渲染，极易误判成渲染 bug。
   - **前置2：必须先打开"强制软解"开关**，否则硬解直出 Surface 根本走不到渲染器，会误判为"Vulkan 没生效"。
   - GLES / Vulkan 双向切换各跑一轮：播放、暂停、seek、EOS、旋转、切后台回前台。
   - **画幅、色彩量程、旋转三项逐项与 GLES 结果对照一致**（截图对比，不是"看起来正常"）。
   - **补充一项**：人为让 Vulkan 初始化失败，确认自动回退 GLES 且诊断面板后端名显示为 OpenGL ES。
   - 报告落 `test-reports/`。
   - 验收：三项对照全部一致 + 回退项通过；双向切换稳定无崩；Vulkan 帧率不明显低于 GLES；报告归档。

## 实施备注（步骤1 落地时记录，后续步骤会用到）

1. **所有 Vulkan 调用必须走 `VK.xxx(...)` 函数表，不得直写 `vkXxx(...)`。** 直写会让链接器重新引入 libvulkan 符号依赖，抵消 dlopen 的保护。改完可用 `llvm-readelf -d` 复核 NEEDED 列表。
2. **`vulkan.h` 只能经 `VEVulkanApi.h` 引入**——`VK_USE_PLATFORM_ANDROID_KHR` 必须在 `vulkan.h` 之前定义。实施时 `VEVulkanVideoRenderer.h` 自行 include 过 `vulkan.h`，导致 `vkCreateAndroidSurfaceKHR` 等 Android 扩展符号全部找不到。
3. **没有 `VE_NOT_SUPPORT` 这个错误码**（`utils/VEError.h` 里不存在），统一用 `VE_INVALID_OPERATION`。
4. **Vulkan 裁剪空间 Y 轴朝下**，`(-1,-1)` 即屏幕左上角，因此**不需要** GLES 那个 `scale(1,-1,1)` 翻转矩阵；纹理坐标表可直接复用。
5. 步骤 2~5 的代码在步骤1 这一版里已一并写出（`VEVulkanVideoRenderer.{h,cpp}` 约 1100 行，含完整渲染路径），但**尚未在真机上跑过一帧**，故仍按未完成计。
6. **（2026-08-08 排查教训）真机验证渲染类改动前，先查设备是否解锁亮屏。** 本次曾把"零渲染"误判为软解路径的既有渲染 bug，实际是测试机锁屏导致 surface 被回收。快速判断法：1080x2400 的纯色截图 PNG 只有约 15KB，正常画面在 100KB 以上。

## 依赖与顺序

- 步骤1 是步骤2 的硬前置（pipeline 需要 SPIR-V）。
- 步骤3、4 依赖步骤2 的 device/pipeline 就绪。
- 步骤6 依赖步骤2~5 渲染器已可用，且依赖 test-console-ui 的诊断面板框架。
- 步骤7 依赖步骤6 的开关（切不了后端就没法对照）与"强制软解"开关（已存在）。

## 风险

1. **Vulkan 初始化失败面广**（设备/驱动/扩展/格式差异）：必须支持回退 GLES 且回退在 UI 可见，禁止静默回退（步骤6 验收项）。
2. **每帧上传三个平面可能比 GLES 更慢**：staging buffer 必须跨帧复用而非每帧分配；实测明显慢于 GLES 即视为步骤4 未通过。
3. **只在软解路径生效**：验证必须先开"强制软解"，否则走不到渲染器（已写进步骤7 前置）。
4. **画幅/量程/旋转等于重走一遍 GLES 踩过的坑**：验收要求逐项对照，而非目视"正常"。
5. **诊断面板依赖**：test-console-ui 尚在 Doing（步骤8 真机自测未完），本 feature 步骤6 的开关要在其框架内新增，注意不与其未完成改动冲突。
