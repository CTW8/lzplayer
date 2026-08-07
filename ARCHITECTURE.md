# LZPlayer 架构文档

> 更新时间: 2026-08-07（high-perf-player Phase 0 同步）
> 工程路径: `/Users/lizhen/Documents/androidproject/lzplayer`

本文描述**当前代码的真实架构**。演进路线见 [docs/high-perf-player-design.md](docs/high-perf-player-design.md)。

---

## 1. 项目概览

基于 FFmpeg 的 Android 音视频播放器，多模块（Java / Kotlin / C++）。

| 项 | 值 |
|----|----|
| 构建 | Gradle 8.12，CMake 3.18.1/3.22.1，NDK 25.1.8937393 |
| ABI | 仅 arm64-v8a |
| SDK | minSdk 24 / targetSdk 33 |
| Native 依赖 | FFmpeg、OpenSSL、libyuv、glm、sonic（已链接，Phase 1 接入）、OpenSL ES、EGL/GLES3 |

### 模块

```
app            主应用（Java，MainActivity + 播放控制 UI）
lzplayer_core  播放器引擎（Java API + JNI + C++ 引擎）      ← 本文重点
MediaSelector  媒体选择器（Kotlin，独立可复用）
MediaPipeline  管道模块（Kotlin 外壳，未实现）
VERecorder     录制模块（Kotlin 外壳，未实现）

依赖：app → lzplayer_core、MediaSelector；其余模块相互独立
```

---

## 2. 调用链路

```
MainActivity
    │
VEPlayer.java (公开 API)  ←── IVEPlayerListener (事件回调)
    │                              ▲
NativeLib.java (JNI 桥)   ───→ NativeLib.EventHandler
    │                              ▲
    │ JNI                          │ postEventFromNative
    ▼                              │
native_PlayerInterface.cpp ── JNIMediaPlayerListener
    │
VEPlayerDriver  (对外状态机：校验 Java 层 API 调用是否合法；
    │            prepare() 的同步语义在这层用条件变量实现)
    │
VEPlayer (AHandler, player_thread)
    └── 内部流程编排：建链 / seek 分阶段 / teardown 分阶段 / 操作串行化
```

**两个状态机职责不同**：`VEPlayerDriver` 管对外 API 合法性（IDLE/INITIALIZED/PREPARED/STARTED/…），`VEPlayer` 管内部流程（含 SEEKING/RELEASING 等编排态）。

---

## 3. C++ 引擎结构

```
cpp/
├── core/                        引擎核心
│   ├── VEPlayer.{h,cpp}         播放器编排（Role 表 / seek / teardown / 串行化）
│   ├── VEPlayerDriver.{h,cpp}   对外状态机 + 回调分发
│   ├── VEDemux.{h,cpp}          本地文件源（VESource 实现）
│   ├── VESourceRegistry.{h,cpp} 媒体源工厂注册表（scheme → 实现）
│   ├── VEAudioDecoder / VEVideoDecoder    软解（IMediaDecoder 实现）
│   ├── VEAudioRender.{h,cpp}    音频渲染调度（IFrameSink + IVEComponent）
│   ├── VEVideoDisplay.{h,cpp}   视频显示调度（IFrameSink + IVEComponent）
│   ├── VEMediaClock / VEAVsync  主时钟与音视频同步判定
│   ├── VEPacketQueue.{h,cpp}    包队列（含字节/时长记账）
│   ├── VEPacket.h / VEFrame.h   AVPacket/AVFrame 的 RAII 包装（禁拷贝）
│   ├── VEMediaDef.h             轨道模型 VETrackInfo / VEMediaInfo
│   └── native_PlayerInterface / VEJvmOnLoad   JNI 层
├── interface/                   抽象接口（扩展点）
│   ├── IVEComponent.h           组件统一命令面
│   ├── IMediaDecoder.h          解码器接口（软解/硬解可换）
│   ├── IMediaSource.h           数据源数据面（read/getFileInfo）
│   ├── VESource.h               媒体源 = AHandler + IMediaSource + IVEComponent
│   ├── IFrameSink.h             帧接收端（推模型 + credit 回执）
│   └── IVideoRender.h / IAudioRender.h   渲染后端
├── platform/android/renders/    VEGLESVideoRenderer、VEAudioSLESRender
├── thread/                      AHandler / AMessage / ALooper（移植自 AOSP）
└── utils/                       Log、VEBundle、VEDef、VEError、TimeUtils…
```

---

## 4. 线程模型

每个组件跑在自己的 `ALooper` 上，命令与数据都以 `AMessage` 投递。

| 线程 | 职责 |
|------|------|
| 调用方线程 | Java/JNI 入口；Driver 做状态校验，prepare/release 同步等待 |
| `player_thread` | VEPlayer 编排：建链、seek 阶段推进、teardown 握手、进度 tick |
| `demux_thread` | 读循环、包队列、seek 定位 |
| `adec_thread` / `vdec_thread` | 音频/视频解码 |
| `audio_render` / `video_render` | 设备喂入 / EGL 上屏 |
| OpenSL ES 回调线程 | 只投递消息，不做业务 |

---

## 5. 数据流（推模型 + credit 流控）

```
文件 → VEDemux ──read(ETrackType)──→ 解码器 ──queueFrame(frame, consumedReply)──→ 渲染器
         ▲                                ▲                                        │
         │  kWhatContinueRead             │            consumedReply（还 credit）  │
         └────────────（拉取触发补货）─────┴────────────────────────────────────────┘
```

- **上游（demux → 解码器）是拉**：解码器 `read()` 取包；空则返回 `VE_NOT_ENOUGH_DATA`，解码器 10ms 轮询重试（NuPlayer `DecoderBase` 的做法）。消费一个包会触发 demux 续读。
- **下游（解码器 → 渲染器）是推**：解码器把帧连同 `consumedReply` 推给 sink；渲染器消费（渲染或丢弃）后原样投回，解码器在途帧计数 -1。计数满则解码循环 park，回执归还即自然复活——**流控是缓冲所有权循环的内生性质，无需显式启停命令**。
- **demux 节流**（仿 ffplay）：总字节 16MB 硬上限；每路目标 1s 时长 / 25 包。两路都够才停读，避免单路满拖死另一路（队头阻塞）。字幕队列不参与该判据。

---

## 6. 关键机制

### 6.1 Role 表与角色状态机

VEPlayer 持有 `std::array<Role, kRoleCount>`，每个槽位是 `{comp, looper, state, componentType}`。

- **命令扇出**：`forEachRole([](Role &r){ r.comp->start(); })`，不再逐个 if 具体组件。
- **回执守卫**：回执只在角色处于对应 `*ING` 态时被接受（`acceptAck`），过期/重复回执直接丢弃。
- **扩展点**：新增组件（硬解解码器、字幕轨）占一个槽位即自动参与 seek/teardown 握手，VEPlayer 编排代码零改动。硬解解码器将同时占 VDEC + VDISPLAY 两个槽位。

### 6.2 分阶段流程

**seek 三阶段**（每阶段等齐回执才推进）：
1. `SEEK_STAGE_PAUSING` — 各组件停止消费数据（等 PAUSE_DONE）
2. `SEEK_STAGE_SEEKING` — demux 定位 + 解码器 flush（等 SEEK_DONE）
3. `SEEK_STAGE_PRIMING` — 重启管线，等首帧上屏（等 FIRST_FRAME）

**teardown 两阶段**：① 停数据流（等 STOP_DONE）→ ② 各线程上释放资源（等 RELEASE_DONE）→ ③ 停 looper、丢对象。资源必须在各自线程销毁（codec ctx / EGL / SLES），故只能消息握手。

每阶段有超时兜底（seek 2s 报错收敛；teardown 800ms 强推，避免 ANR）。

### 6.3 防过期三道闸

| 机制 | 位置 | 挡什么 |
|------|------|--------|
| `plGen` 管线代次 | notify 模板（VEPlayer） | 上一代管线组件的迟到事件 |
| `epoch` | 解码器 / 渲染器 | flush/seek 前投递的解码/渲染消息与迟到回执 |
| `queueGen` | 渲染器接收侧 | flush/seek 前在途的旧帧 |

### 6.4 操作串行化

`PendingAction` 队列（仿 NuPlayer `mDeferredActions`）：长流程（seek / setDataSource / prepare / reset / release）不重叠；流程忙时入队，前一个完成后按序执行。连续拖动进度条时队尾 SEEK 会合并，只做最后一次。

### 6.5 时钟与同步

- `VEMediaClock`：音频 pts 锚点 + 实时外推 + 速率。pause 先结算再冻结；`resetTo(pts, keepPaused)` 处理暂停态 seek。
- `VEAVsync`：视频 pts 与主时钟比较——同步窗 40ms 内直接渲染，领先则等待，落后超 100ms 丢帧追赶，差值超 500ms 视为时钟不可信、退化为按帧率出帧。
- 纯视频文件无音频锚点时，起播手动 `resetTo(0)`。
- 音频时钟按"正在被听到"的位置打点：入队 pts 减去设备在途时长与器件延迟。

### 6.6 轨道模型

`VEMediaInfo` 持有 `vector<VETrackInfo>` 与活跃轨下标。要点：

- **codecParams 深拷贝自持**，析构统一释放——源 release 后解码器仍安全（消除悬垂指针）。
- 默认轨用 `av_find_best_stream` 选（多音轨文件不会选错）。
- 视频轨从 `AV_PKT_DATA_DISPLAYMATRIX` 解出 `rotationDegrees`，渲染器据此摆正竖拍视频。
- 文本字幕轨（SRT/ASS/MOV_TEXT/WebVTT）登记进列表；位图字幕（PGS/DVB）过滤不上报。

### 6.7 扩展点一览

| 维度 | 接口 | 工厂 |
|------|------|------|
| 媒体源 | `VESource` | `VESourceRegistry`（scheme → 实现） |
| 解码器 | `IMediaDecoder` | Phase 2 的 `VEVideoDecoderFactory` |
| 帧处理链 | `IFrameSink` | 链式插入 processor，credit 回执透传 |
| 渲染后端 | `IVideoRender` / `IAudioRender` | 按系统版本/配置选择 |

**事件通道纪律**：新增事件一律 `postNotify(type, event, args)` → `VEPlayer::onComponentEvent` → Java 回调链路；事件号在 `utils/VEDef.h` 集中分配。禁止组件间直接互调或另起线程回调。

---

## 7. 事件与错误码（utils/VEDef.h）

- 组件 → 播放器：`SEEK_DONE`/`STOP_DONE`/`PAUSE_DONE`/`FLUSH_DONE`/`RELEASE_DONE`/`PREPARE_DONE`/`FIRST_FRAME`/`EOS`/`PROGRESS`/`ERROR`/`SELECT_TRACK_DONE`/`SUBTITLE(_CLEAR)`/`BUFFERING_*`
- 播放器 → Java：`VE_PLAYER_NOTIFY_EVENT_ON_*`（PROGRESS/PREPARED/EOS/ERROR/INFO/COMPLETION/SEEK_DONE/TRACK_CHANGED/SUBTITLE/BUFFERING_*）
- 错误码：`VEError.h` 通用码 + `VEDef.h` 播放器专用码（OPEN_DEMUX_FAILED / NETWORK_IO / NETWORK_TIMEOUT / UNSUPPORTED_TRACK）

---

## 8. 构建

```bash
./gradlew assembleDebug          # 构建
./gradlew :lzplayer_core:assembleDebug   # 只构建引擎
./gradlew installDebug           # 安装
```

CMake 用 `file(GLOB_RECURSE)` 收集源文件——**增删 .cpp 后需 touch CMakeLists.txt 触发重新 configure**，否则 ninja 会引用已删除的文件。

---

## 9. 已知限制

- 变速（`setPlaySpeed`）当前返回不支持（Phase 1 接入 sonic 后开放）。
- 仅软解；硬解与 Surface 零拷贝在 Phase 2。
- 仅本地文件；网络源在 Phase 3。
- 音频时钟的设备延迟是估算值（SLES 拿不到精确呈现位置），Phase 4 换 AAudio 后用 `getTimestamp` 根治。
- 视频帧上屏时机靠消息延时，未与 vsync 对齐（Phase 2 硬解直出 Surface 后自然解决）。
- 多轨切换与字幕在 Phase 5。
