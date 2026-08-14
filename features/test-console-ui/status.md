# test-console-ui 进度

> 最后更新: 2026-08-14
> 总体状态: Doing
>
> 口径说明：步骤 1~7 的代码实施已全部完成且 `./gradlew clean assembleDebug` 通过（app-debug.apk 36MB 已产出），但**均未做真机验证**（执行时 `adb devices` 为空，无连接设备），因此一律停留在 Doing 段（标注"代码完成，待真机验证"），不标 Done。步骤8 真机自测是唯一阻塞前七步一并转 Done 的事项。

## Done

（无）

## Doing

- [ ] 步骤1: 主控台骨架 — **代码完成，待真机验证**
  - 设计 token 落地：`console_colors.xml` / `console_dimens.xml` / `console_styles.xml`；8 个 drawable（按钮 / 面板 / chip / 三层进度条 / thumb / sheet 背景 / 轨道项）。
  - `SmpteBarsView.kt`：SMPTE 彩条改为自定义 View 实现（原计划的 drawable 方案行不通——layer-list 是堆叠不是并排）。
  - `ConsoleActivity.kt`：源栏 + SurfaceView + 常驻 HUD（解码路径按 HW/SW 着色、状态与速率）+ 双层进度条与时间码 + 传输控件 + 六档速率 + 四宫格入口 + 精准跳转对话框。
  - **偏差（实施时决定并回填）**：`Theme.LZConsole` **不用 DayNight**——视频区永远是暗的，跟随系统切浅色反而妨碍画质判断。

- [ ] 步骤2: native 前置接口补齐 — **代码完成，待真机验证**
  - **plan.md 原判断"getStats 与两个 force setter 已具备"有误**：实际统计数据源整条链路都缺，本步补齐范围远大于原计划。
  - VEDemux 新增 `getQueueDepth(ETrackType)` / `getBufferedDurationUs()`（音视频两路取短板）。
  - VEAVsync 新增 `getLastDiffUs()`（视频 pts − 主时钟）。
  - VEVideoDisplay 与 VEMediaCodecVideoDecoder 各加原子计数 `mRenderedFrames` / `mDroppedFrames`，在上屏与丢帧处累加。
  - VEAudioRender 新增 `setForceSles(bool)`（prepare 前设置）与 `backendName()`，后端选择处接入强制开关。
  - VEPlayer 新增 `getStatsJson()`（聚合 state/decoder/codec/audioBackend/avOffsetMs/renderedFrames/droppedFrames/audioQueue/videoQueue/bufferedMs/source/speed/buffering/positionMs/durationMs）、`setForceSoftwareDecoder` / `setForceSlesAudio`。
  - 新增 `mUserForceSoftware` 与 `mDecoderPolicy.forceSoftware` 分离：后者会被运行期 fallback 置位，换片源时按用户意图重置，避免 fallback 记忆粘住。
  - Driver / JNI / NativeLib / VEPlayer.java 全链路透出 `getStats` / `setForceSoftwareDecoder` / `setForceSlesAudio`；新增 `PlayerStats.java`（JSON 解析 + 便捷判定）。
  - `getTrackInfoJson` 补齐 `codecName/sampleRate/channels/width/height/rotation` 序列化；`TrackInfo.java` 补对应字段与 `spec()` 描述方法（登记时指出的缺口已闭合）。

- [ ] 步骤3: 轨道面板 BottomSheet — **代码完成，待真机验证**
  - `TrackSheet.kt`：三组轨道带类型条纹（视频蓝 / 音频绿 / 字幕琥珀）；视频轨置灰只读；字幕组含"关闭"项与外挂加载入口。
  - 切轨 / seek 完成后在标题旁显示耗时，按 ≤200ms 着色。

- [ ] 步骤4: 字幕 overlay — **代码完成，待真机验证**
  - `SubtitleOverlayView.kt`：描边 + 填充两遍绘制（纯阴影在亮场景会糊）；`bottomInsetPx` 供控件浮出时抬高字幕。

- [ ] 步骤5: 诊断面板 BottomSheet — **代码完成，待真机验证**
  - `DiagnosticsSheet.kt`：八项读数两列栅格，按阈值着色（音视频偏移 ±40ms、丢帧率、缓冲水位）。
  - 两个策略开关用"开/关"文字而非纯色块（灰度截图下可读），标注"下次 prepare 生效"。
  - `EventLog.kt`：环形事件流（容量 200），事件名与 `VEDef.h` 常量同名，支持导出纯文本贴进回归报告；面板内一键复制事件流。
  - 读数刷新搭 `onProgress` 的 500ms 节奏，未另开定时器（符合风险 7 的约束）。

- [ ] 步骤6: 横屏沉浸模式 — **代码完成，待真机验证**
  - 控件 3s 无操作自动隐藏、HUD 常驻、字幕随控件显隐抬升、缓冲遮罩。

- [ ] 步骤7: 网络 URL 输入与缓冲态 UI — **代码完成，待真机验证**
  - 源栏支持 URL 直接输入；本地选择走 `MediaSelectorActivity.EXTRA_ALLOWED_TYPES` / `EXTRA_MAX_SELECT_COUNT` / `EXTRA_SELECTED_FILES` 实际常量。
  - Manifest 新增 INTERNET / ACCESS_NETWORK_STATE 权限与 `usesCleartextTraffic`（局域网限速服务器测试需要）。
  - 缓冲遮罩与百分比由 BUFFERING_START/UPDATE/END 驱动。

## Todo

- [ ] 步骤8: 真机自测（adb-ops 逐屏截图核对 A/B/C/D 四块屏幕 + 灰度可读性 + Phase 0~5 能力覆盖清单），报告落 `test-reports/` —— **唯一阻塞步骤 1~7 标 Done 的事项**。执行时 `adb devices` 为空，需插设备后再做。
  - **2026-08-08 前置变更（perf-metrics 步骤5 已改动本 feature 的文件）**：`DiagnosticsSheet.kt`
    已由单页诊断面板改为**六分页性能面板**（概览/启播/Seek/稳态/资源/日志），
    `ConsoleActivity` 新增 `statsJsonProvider` / `startupTraceProvider` 两个构造参数。
    **步骤8 的 C 屏核对必须以当前六分页形态为准**，不能按本 feature 原设计的八项读数单页去对。
    perf-metrics 已在其步骤5 做过逐页真机截图核对，可作参考。另：`ConsoleActivity` 缺
    `launchMode="singleTop"` 的缺陷（`onNewIntent` 从未被调用）已由 perf-metrics 顺修，无需重复排查。
  - **2026-08-14 前置变更（console-ui-v2 将重构本 feature 的主界面）**：新登记的
    **console-ui-v2** 会重写 `activity_console.xml` 与 `ConsoleActivity.kt` 的布局
    —— 源栏折叠、常驻两行八格仪表带、播放键缩为 26px 内嵌进度行、面板入口四等宽同样式、
    次要控件改纯文字、横屏沉浸时 chip 与 HUD 常驻。
    **因此步骤8 的 A 屏（主控台）与 D 屏（横屏沉浸）核对标准即将作废**，
    按当前布局先做一遍是白做。**建议步骤8 与 console-ui-v2 步骤6（真机截图核对）
    合并成一轮真机工作**，A/D 两屏按新布局重写核对项，B/C 两屏核对项不受影响。
- [ ] 代码提交（CLAUDE.md 要求提交前需用户明确同意；当前全部改动未提交）。

## 待记录的决策（已回填）

- **Kotlin 与旧界面的关系**：app 模块启用 Kotlin 插件（根 gradle 已有声明）+ core-ktx；新界面全用 Kotlin。旧 `MainActivity.java` **保留作对照但从桌面入口摘掉**（改为 `exported` 且无 intent-filter，需要时 `adb am start` 拉起），`ConsoleActivity` 成为 LAUNCHER。

## 顺带修复（归属 high-perf-player）

- `VEPlayer::rebuildVideoAsSoftware` 原本把 `VE_INFO_DECODER_FALLBACK(0x3001)` 直接当事件号发 `notifyInfo`，而 JNI 分发与 Java `EventHandler` 都不认识该号，事件被当未知消息丢弃——即"硬解回退"永远到不了 UI。已改为走 `ON_INFO` 通道、`VE_INFO_DECODER_FALLBACK` 放 `arg1`。该缺陷属 high-perf-player Phase 2，已在其 status.md 记录（修复动作发生在本 feature 实施中）。

## 已知遗留

- `startActivityForResult` 用的是废弃 API（与既有 MainActivity 保持一致，未改造成 ActivityResultLauncher）。
