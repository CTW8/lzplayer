# vulkan-renderer 进度

> 最后更新: 2026-08-08
> 总体状态: Doing（步骤7 真机验证被设备锁屏阻塞，等待解锁）

## 备注

- **本 feature 不含音频工作。** OpenSL ES 音频后端已实现且真机验证通过（VEAudioRender 按版本选 AAudio/SLES，诊断面板"强制 OpenSL ES"可双向切换；实测切 SLES 后 10 秒推进 9.88 秒，比例 1.0，静音仅起播首秒一次）。仅在设计文档中记录该既有能力，不派生步骤。
- **只影响软解路径**：硬解走 MediaCodec 直出 Surface，不经 GLES 也不经 Vulkan。价值是渲染后端可插拔 + 为未来滤镜铺路，不是性能优化。

- **链接策略已修订**：不直接链接 libvulkan，改 `dlopen("libvulkan.so")` + `vkGetInstanceProcAddr` 派生约 70 个入口（`VEVulkanApi.{h,cpp}` X-macro 函数表）。避免 libvulkan 成为 DT_NEEDED 硬依赖导致整个 native 库加载失败。**所有 Vulkan 调用必须走 `VK.xxx(...)`，不得直写 `vkXxx(...)`。**

- **排查教训（2026-08-08，务必先看这条）**：真机验证渲染类改动**前置检查设备已解锁亮屏** —— `dumpsys power` 的 `mWakefulness=Awake` 且 `dumpsys window` 的 `isKeyguardShowing=false`。锁屏/息屏会让 Activity 窗口 surface 被销毁（日志表现为 `changeSurface new surface 0x0` → `surface detached`，之后不再重建），GLES 与 Vulkan 都会零渲染。本次曾据此**误判为"软解路径渲染不出画面的既有 bug"**，实际只是设备锁屏。快速判断法：1080x2400 的纯色截图 PNG 只有约 15KB，正常画面在 100KB 以上。

## Done

- [x] 步骤1: shader 与 SPIR-V 内嵌 + Vulkan 动态加载层 (2026-08-08) — 落地 `shaders/yuv.vert`、`yuv.frag`（GLSL 源留档）、`VEVulkanShaders.h`（SPIR-V 内嵌 187 + 322 words，注释含重新生成命令）、`VEVulkanApi.{h,cpp}`（方案外新增的 dlopen 函数表）；编译链接通过，`llvm-readelf -d` 确认产物 NEEDED 列表不含 libvulkan / libaaudio。
- [x] 步骤6: VEVideoRenderFactory + 策略开关全链路 (2026-08-08) — 新增 `core/VEVideoRenderFactory.{h,cpp}`（`Policy{preferVulkan}`，默认 GLES，要 Vulkan 但初始化失败自动回退 GLES 并经 `outUsedVulkan` 如实告知实际后端，两者都失败才返回 nullptr，`backendName(bool)` 供诊断显示）；`VEVideoDisplay` 两处 `make_shared<VEGLESVideoRenderer>()`（onPrepare 与 onSurfaceChanged 延迟创建）统一改走工厂，新增 `mRenderPolicy`/`mUsedVulkan` 与 `setPreferVulkan(bool)`/`renderBackendName()`，头文件 include 由 IVideoRender.h 改为 VEVideoRenderFactory.h；策略链路照抄 setForceSlesAudio：`VEPlayer::setPreferVulkanRender`(`std::atomic<bool> mPreferVulkanRender`) → `VEPlayerDriver` → `nativeSetPreferVulkanRender`（已注册进 VEJvmOnLoad.cpp 方法表）→ `NativeLib.java` → `VEPlayer.java`，`setupVideoChain()` 在 `mVideoRender->prepare()` **之前**调用 `setPreferVulkan()`；`DiagnosticsSheet` 加第三个开关「Vulkan 渲染（需同时强制软解）」（构造参数增 `preferVulkan`/`onPreferVulkan`），`ConsoleActivity` 增 `preferVulkan` 字段并在 openSource 新建播放器时与另两个策略一起重新下发。**方案外新增**：`ConsoleActivity` 两个 intent extra `EXTRA_FORCE_SOFTWARE = "software"` / `EXTRA_PREFER_VULKAN = "vulkan"`，供回归脚本自动化下发策略（已补进 plan 与设计文档 2.1）。编译通过。

## Doing

- [ ] 步骤2: VEVulkanVideoRenderer 骨架（instance/device/queue/surface/swapchain/renderpass/pipeline） — **真机部分验证通过**（小米 fa04e593 / Android 16 / Adreno 740）：`VulkanApi: loader ready`（dlopen 方案真机可用）、`using GPU "Adreno (TM) 740", queue family 0`、`initialize success, view 1080x1579 rotation 0, swap 1080x1579`，instance/device/surface/swapchain/renderpass/pipeline/framebuffer 全链路创建成功且无 VkResult 报错；`VEVideoRenderFactory: using Vulkan renderer` 确认工厂选中 Vulkan 未回退。**但至今未成功呈现过一帧**，清屏出帧、反复 init/uninit 不崩等验收项仍未验证。
- [ ] 步骤3: YUV 三平面上传与描述符集、push constant 色彩参数 — 代码已写、Vulkan 环境已在真机就绪，但因未出帧，色彩/量程比对未验证。
- [ ] 步骤4: renderFrame 命令录制/提交/present + 同步 — 代码已写；**`renderFrame` 的提交/present 路径至今未被执行过**（本次因设备锁屏 surface 被回收，见上方排查教训）。
- [ ] 步骤5: changeSurface 的 swapchain 重建与 surface 置空处理 — 代码已写，旋转/切后台/置空回设等验收项未验证。
- [ ] 步骤7: 真机验证 — **阻塞：测试机锁屏无法解锁。** `dumpsys window` 显示 `isKeyguardShowing=true` 且 `mCurrentFocus=NotificationShade`（通知栏卡住收不回），`wm dismiss-keyguard`、上滑、熄屏亮屏重试均无效，疑似需要 PIN/图案。锁屏下 Activity surface 被销毁，GLES 与 Vulkan 均零渲染。**解锁后待验收**（原验收项不变）：先开强制软解 → GLES/Vulkan 双向切换，画幅、色彩量程、旋转三项与 GLES 逐项一致；另补一条"Vulkan 初始化失败能正确回退 GLES 且诊断面板后端名显示为 OpenGL ES"。

## Todo

（无；步骤 2~5、7 的解锁与验证完成后本 feature 即可收口）
