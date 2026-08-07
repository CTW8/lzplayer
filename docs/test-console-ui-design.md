# LZPlayer 测试台 UI 设计文档（test-console-ui）

> 创建日期: 2026-08-08
> 实施计划: ../features/test-console-ui/plan.md

## 1. 背景与目标

### 背景

high-perf-player 六个阶段（Phase 0~5）的 native 代码已全部完成，待真机回归。这轮演进带来了大量新能力：

- Phase 1：变速播放（0.5x~2.0x，音调不变）、精准 seek 提速
- Phase 2：MediaCodec 硬解 + Surface 零拷贝、软解 fallback（`VE_INFO_DECODER_FALLBACK`）
- Phase 3：网络源（HTTP/HTTPS）、三水位缓冲与 `BUFFERING_START/UPDATE/END` 事件
- Phase 4：AAudio/OpenSL ES 双音频后端、调度精修
- Phase 5：多音轨切换、字幕轨切换/关闭、外挂字幕、`ON_SUBTITLE`/`ON_SUBTITLE_CLEAR`

而 app 模块的 UI 仍停留在旧版：仅有「选择文件 / 播放 / 暂停 / 停止 / 进度条 / SurfaceView」，单个 `MainActivity.java`。新增能力**在界面上完全不可达**，也**无处读出内部状态**，导致：

1. 新能力无法手工触发，回归只能靠 logcat 反推；
2. 真机回归缺少统一的操作载体，lzplayer-test-expert 的用例难以稳定复现；
3. 关键指标（解码路径、音视频偏移、丢帧、队列深度）不可见，性能问题定位靠猜。

### 目标

做一个**工程测试台**界面，不是消费级播放器。判定标准只有一条：

> **每一项已实现的能力，都有一个可以按到的控件；每一项内部状态，都有一处当场可读的读数。**

同时这个界面本身就是真机回归的操作载体——lzplayer-test-expert 的用例直接以它为 UI 入口，截图即证据。

### 非目标

- 不做消费级播放体验（手势调音量/亮度、播放列表、历史记录、缩略图预览等一概不做）
- 不做视频轨切换（native 本期不支持，UI 上只读并灰掉）
- 不做美观优先的动效；一切服从"读数可信、状态可辨"

## 2. 设计语言

设计稿已以 Artifact 形式产出，本节概述其规则，实施时以此为准。

### 定位隐喻：视频测试仪器

界面参照广播级视频测试仪器（波形监视器 / 矢量示波器）的观感，而非流媒体 App：

- **SMPTE 彩条**作为结构分隔元素（区块之间的分隔条），既是视觉锚点也是色彩参考
- **等宽字体**承担全部标签与读数；数字启用 tabular nums（等宽数字），保证读数跳动时字符不横向抖动
- 布局密度偏高，信息优先于留白

### 色彩

主色与语义色**严格分家**，避免"主色即成功色"的歧义：

| 角色 | 色值 | 说明 |
|------|------|------|
| 主色 | `#3FA9C4` | 去饱和 SMPTE 青，用于强调、选中态、主按钮 |
| ok | `#52B788` | 正常/成功/激活 |
| warn | `#D9A441` | 警告（软解回退、缓冲中、偏移偏大） |
| crit | `#E5544B` | 错误/危险 |
| 底色 | `#0E1217` | 页面背景 |
| 面板 | `#171C23` | 卡片/面板背景 |
| 发丝线 | `#272E38` | 1px 分隔线、边框 |
| 正文 | `#D3DAE4` | 主要文字 |
| 弱化 | `#8792A2` | 次要文字、标签 |

轨道条纹沿用剪辑软件约定，便于一眼区分轨道类型：

| 轨道 | 色值 |
|------|------|
| 视频 | `#4A7FD4` |
| 音频 | `#52B788` |
| 字幕 | `#D9A441` |

### 无障碍原则：状态不只靠颜色

灰度截图（以及色觉障碍用户）下同样必须可读，因此每个状态都有**形状或文字**冗余：

- 硬解/软解：显示 `HW` / `SW` 字样，不只是绿/黄
- 缓冲中：出遮罩层 + 百分比数字，不只是变色
- 错误：带独立标识符（`ERR` 前缀 + 错误码），不只是红字

这条规则同时服务于回归测试——截图是灰度或压缩后仍可判读。

## 3. 屏幕设计（四块）

### A. 主控台（竖屏主界面）

自上而下：

1. **源栏**：两种输入方式并列
   - 本地路径：走 MediaSelector 选文件
   - 网络 URL：文本输入框 + 载入按钮
2. **SurfaceView 视频区**：固定宽高比容器
3. **常驻 HUD**（叠在视频区上）
   - 左上：解码路径 `HW`/`SW` + codec 名（如 `HW h264`）
   - 右上：状态机状态（IDLE/PREPARED/STARTED/PAUSED/...）+ 当前速率（如 `1.25x`）
4. **字幕 overlay**：叠在视频区下部，带描边文本
5. **双层进度条 + 时间码**
   - 底层：已下载/已缓冲进度（网络源）
   - 上层：已播放进度
   - 右侧时间码 `当前 / 总时长`，等宽数字
6. **传输控件**：播放暂停（合一）/ 停止 / 循环开关
7. **速率分段控件**：0.5 / 0.75 / 1.0 / 1.25 / 1.5 / 2.0 六档
8. **四宫格入口**：轨道 / 字幕 / 跳转 / 诊断

### B. 轨道面板（BottomSheet）

- **视频轨**：只读列表并灰掉，标注"本期不支持切换"
- **音频轨**：单选列表，每项显示 语言 / codec / 采样率 / 声道
- **字幕轨**：单选列表，第一项固定为「关闭」
- **加载外挂字幕**：按钮，选文件后调 `addExternalSubtitle`
- **切轨耗时**：切换完成（收到 `ON_TRACK_CHANGED`）后在该项原位显示耗时毫秒数，用于对照验收线「硬解切轨 < 200ms」

### C. 诊断面板（BottomSheet）

**八项实时读数**（跟随既有进度 tick 刷新，不另开定时器）：

1. 解码路径（HW/SW + codec）
2. 音频后端（AAudio / OpenSL ES）
3. 音视频偏移（ms，带符号）
4. 丢帧 / 总帧
5. 包队列深度（音 / 视）
6. 缓冲水位（网络源，%或秒）
7. 源类型（local / http / https）
8. 播放器状态

**两个强制开关**：

- 强制软解
- 强制 OpenSL ES

二者改变的是**下次建链策略**，需重新 prepare 才生效——UI 上必须明说这一点（开关下方常驻说明文字），避免误判为即时生效。

**事件流列表**：按时间倒序列出收到的 native 事件，事件名与 `VEDef.h` 中的常量**同名**（如 `ON_SEEK_DONE`、`ON_BUFFERING_UPDATE`、`VE_INFO_DECODER_FALLBACK`），便于与 logcat 逐条对照。

**导出日志**：把事件流 + 当前读数快照导出为文本文件，可用 adb pull 取回。

### D. 横屏沉浸

- 控件 3 秒无操作自动隐藏，HUD **常驻**（回归时随时能读解码路径与状态）
- 字幕在控件浮出时上移，避免被传输控件遮挡
- 用途：验证旋转处理、色彩量程（Phase 0.A 的 limited/full range + BT.601/709 修正）、字幕遮挡

## 4. 涉及模块与接口

### 已具备的 Java API（`lzplayer_core/.../VEPlayer.java`）

`init(String path)` / `prepare()` / `prepareAsync()` / `start()` / `pause()` / `resume()` / `stop()` / `seekTo(double)` / `setLooping(boolean)` / `setPlaySpeed(float)` / `getTrackInfo()` / `selectTrack(int)` / `deselectTrack(int)` / `addExternalSubtitle(String)` / `getDuration()` / `getCurrentPosition()` / `setSurface(Surface,int,int)` / `registerListener(IVEPlayerListener)` / `release()`

回调链路：Native → `NativeLib.EventHandler` → `IVEPlayerListener{ onInfo(type,msg1,obj), onError(type,msg1,msg2,msg3), onProgress(double) }`

事件常量（`lzplayer_core/src/main/cpp/utils/VEDef.h`）：`ON_PROGRESS/PREPARED/EOS/ERROR/INFO/FIRST_FRAME/COMPLETION/SEEK_DONE/TRACK_CHANGED/SUBTITLE/SUBTITLE_CLEAR/BUFFERING_START/BUFFERING_UPDATE/BUFFERING_END`，以及 `VE_INFO_DECODER_FALLBACK`。

### 诊断数据源（2026-08-08 核对工作区代码后修订）

初稿假设诊断读数与策略开关都缺 native 接口，实际核对后**大部分已具备**：

- `VEPlayer.getStats()` → `PlayerStats`，字段：`state / decoder / codec / audioBackend / avOffsetMs / renderedFrames / droppedFrames / audioQueue / videoQueue / bufferedMs / source / speed / buffering / positionMs / durationMs`。JNI `nativeGetStats` → `VEPlayer::getStats()` 已实现，**拉模式**，UI 按进度 tick 取用即可，不必新增 native 定时器。诊断面板八项读数由此全部有真实来源。
- `VEPlayer.setForceSoftwareDecoder(boolean)` / `setForceSlesAudio(boolean)` 已透出 JNI/Java，语义为**下次 prepare 建链生效**。

### 仍需新增的 native 改动（前置子任务）

**轨道 JSON 字段补齐**：`VEPlayer::getTrackInfoJson()` 目前只输出 `index/type/lang/title/codec/active`，缺轨道面板要显示的**采样率与声道数**——`VETrackInfo` 内部已有 `sampleRate`/`channels`（`VEDemux.cpp` 填充），只是没序列化。需补 JSON 字段并扩展 `TrackInfo.java` 解析。codec 名可在 Java 侧由 `codecId` 映射兜底。

### app 模块现状与语言选择

- 现状：`app/src/main/java/com/example/lzplayer/MainActivity.java`，单 Activity + `activity_main.xml`，Java。
- CLAUDE.md 要求新 UI 代码优先 Kotlin。**本 feature 新增界面代码一律用 Kotlin**。
- 与既有 `MainActivity.java` 是共存（新增 `TestConsoleActivity`，旧的保留作对照）还是直接替换，**由实施时决定并在 plan/status 中记录**。倾向先共存，回归通过后再决定是否删旧。

## 5. 验收标准

1. Phase 0~5 的每项能力在 UI 上都有可按到的控件（逐项对照 high-perf-player 的六阶段能力清单）
2. 诊断面板八项读数全部有真实数据来源，无占位假数据
3. 四块屏幕在真机上截图核对通过，灰度下状态仍可判读
4. 切轨耗时读数可用于判定「硬解 < 200ms」验收线
5. 事件流列表的事件名与 `VEDef.h` 常量逐条对得上

## 6. 风险与依赖

| 风险/依赖 | 说明 | 应对 |
|-----------|------|------|
| ~~native 统计接口缺失~~ | 已消解：`getStats()`/`PlayerStats` 全链路已通 | UI 搭进度 tick 拉取即可 |
| ~~策略开关缺失~~ | 已消解：两个 force setter 已透出 | UI 必须明写"下次 prepare 生效"，禁止假开关 |
| TrackInfo 字段不足（仍在） | 轨道 JSON 缺采样率/声道 | 步骤2 补 JSON 序列化 + Java 解析 |
| Java/Kotlin 混合 | app 现为 Java 单 Activity | 新代码 Kotlin，共存或替换实施时定并记录 |
| high-perf-player 未回归 | 本 feature 依赖的 native 能力尚未真机验证 | 本测试台正是回归载体；若回归中发现 native bug，归到 high-perf-player 修，不在本 feature 混改 |
| 硬解厂商差异 | fallback 路径需在 UI 上可见 | HUD 的 HW/SW 字样 + 事件流里的 `VE_INFO_DECODER_FALLBACK` 双重体现 |
