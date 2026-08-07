# high-perf-player 方案设计（详细版）

> 创建日期: 2026-08-07（详细化: 2026-08-07）
> 状态: 已登记，Todo
> 实施计划: ../features/high-perf-player/plan.md

## 背景与目标

将 lzplayer_core 升级为"高性能、低 CPU 占用，支持本地文件 + 网络播放 + 变速 + 精准 seek + 播放中音轨切换 + 字幕"的播放器。

方案基于 2026-08-07 的全量架构 review，结论：现有 NuPlayer 式消息驱动架构（AHandler/AMessage/ALooper + 推模型 + credit 流控）是健康的，**按此地基演进，不推倒重来**。

### 量化指标

| 指标 | 目标 |
|------|------|
| 1080p30 H.264 播放 CPU（硬解接管后） | 中端 arm64 机型整机 < 15%，以 simpleperf 实测为准，每阶段留 before/after 数据 |
| 本地起播 | < 300ms |
| 本地精准 seek 到画面（硬解） | < 300ms |
| 变速 | 0.5x ~ 2.0x，音调不变（time-stretch），变速下音画同步不劣化 |
| 网络播放 | HTTP(S) 渐进式下载（MP4/MKV/FLV/TS）；起播/卡顿水位可配；BUFFERING 事件上报 UI |
| 扩展性 | 媒体源/解码器/帧处理链/渲染后端四个维度可插拔（详见「扩展性设计」） |
| 多轨与字幕 | 播放中音轨切换（画面定格不黑屏，硬解下 < 200ms）；字幕显示与切换（内嵌文本字幕 + 外挂字幕） |

### 既定架构决策（约束）

- 网络 IO 不由 FFmpeg 负责：FFmpeg 只做 demux，字节由自实现网络模块经自定义 AVIOContext（read/seek 回调）注入。
- 回调链路保持 Native → notify → `NativeLib.EventHandler` → `IVEPlayerListener`，不另起通道。
- 新增播放器能力走 `VEPlayerDriver` → `AMessage` 事件分发，JNI 层不做业务逻辑。

## 现状机制术语表（详细设计中反复引用）

| 术语 | 含义 | 出处 |
|------|------|------|
| credit | 解码器以在途帧计数为闸的流控：`queueFrame` 时 +1，渲染器消费后投回 consumedReply 时 -1，满则 park | VEAudioDecoder/VEVideoDecoder `mInFlightFrames` |
| epoch | 解码器/渲染器侧的代次号，flush/seek 递增，作废在途的解码/渲染消息与迟到回执 | `mEpoch` |
| queueGen | 渲染器接收队列代次，挡 flush 后在途的旧帧 | `mQueueGen` |
| plGen | 管线代次，盖在 notify 模板上，挡上一代管线的迟到事件 | VEPlayer `mPipelineGen` |
| RoleState | 每组件一个角色状态，回执只在对应 *ING 态被接受（防过期/重复回执） | VEPlayer `acceptAck` |
| PendingAction | 长流程（seek/reset/release/prepare）串行化队列，流程忙时入队 | VEPlayer `mPendingActions` |
| Flow 超时 | 每个流程阶段的超时兜底消息，带 `mFlowSeq`，阶段推进即作废 | VEPlayer `postFlowTimeout` |

## 全局新增清单（各 Phase 汇总）

### 新接口 / 新类

| 名称 | 位置 | Phase |
|------|------|-------|
| `IVEComponent` | interface/ | 0 |
| `IMediaDecoder : IVEComponent` | interface/ | 0 |
| `VESourceRegistry`（scheme 注册表） | core/ | 0 |
| `VETrackInfo` / `VEMediaInfo` 轨道列表化 | core/VEMediaDef.h | 0 |
| `VESonicProcessor` | core/ | 1 |
| `VEMediaCodecVideoDecoder : IMediaDecoder` | platform/android/decoders/ | 2 |
| `VEVideoDecoderFactory` | core/ | 2 |
| `IDataSource` / `VEHttpDataSource` / `VEBufferedDataSource` | core/net/（新目录） | 3 |
| `VENetworkSource : VESource` | core/net/ | 3 |
| `VEAAudioRender : IAudioRender` | platform/android/renders/ | 4 |
| `VESubtitleTrack : AHandler, IVEComponent` | core/ | 5 |

### 新事件（VEDef.h 集中分配）

| 事件 | 方向 | Phase |
|------|------|-------|
| `VE_NOTIFY_EVENT_BUFFERING_START / END / UPDATE`（arg1=百分比） | 源 → VEPlayer → Java | 3 |
| `VE_NOTIFY_EVENT_TRACK_CHANGED`（arg1=trackIndex） | VEPlayer → Java | 5 |
| `VE_NOTIFY_EVENT_SUBTITLE`（params=文本）/ `VE_NOTIFY_EVENT_SUBTITLE_CLEAR` | 字幕组件 → VEPlayer → Java | 5 |
| `VE_NOTIFY_EVENT_SELECT_TRACK_DONE`（内部回执） | demux → VEPlayer | 5 |
| 组件类型 `E_COMPONENT_TYPE_SUBTITLE` | — | 5 |

### 新错误码

`VE_PLAYER_ERROR_NETWORK_IO`、`VE_PLAYER_ERROR_NETWORK_TIMEOUT`（Phase 3）；`VE_PLAYER_ERROR_UNSUPPORTED_TRACK`（Phase 5）；信息类 `VE_INFO_DECODER_FALLBACK`（Phase 2，硬解回退软解时通知上层）。

### Java API 变更总表（VEPlayer.java / NativeLib.java / IVEPlayerListener）

| API | 说明 | Phase |
|-----|------|-------|
| `setPlaySpeed(float)` | 已有链路，打通（当前底层返回不支持） | 1 |
| `setDataSource(String)` 接受 http/https URL | 接口不变，语义扩展 | 3 |
| `getTrackInfo(): TrackInfo[]` | 新增；JNI 返回 JSON 字符串，Java 侧用 org.json 解析为 `TrackInfo{index, type, lang, title, codec}` | 5 |
| `selectTrack(int)` / `deselectTrack(int)` | 新增 | 5 |
| `addExternalSubtitle(String path)` | 新增 | 5 |
| Listener 新回调：`onTrackChanged(int)`、`onSubtitle(String)`/`onSubtitleClear()`、`onBufferingUpdate(...)` | 经既有 `postEventFromNative` 通道新增 msg 类型 | 3/5 |

## 扩展性设计

原则：扩展靠"接口 + 工厂 + 既有 notify 事件通道"三件套，新增能力实现接口注册进工厂即可，禁止修改 VEPlayer 编排逻辑或另起回调通道。

1. **媒体源可插拔（VESource）**：createSource 由 if-else 改为 scheme → 工厂函数注册表（file→VEDemux、http/https→VENetworkSource，未来 rtmp/hls 注册即接入）。数据面统一 IMediaSource::read，命令面统一 VESource 命令接口 + postNotify 回执。
2. **解码器可插拔（IMediaDecoder）**：软解（FFmpeg）/硬解（MediaCodec）统一接口，工厂按 codec/分辨率/白名单选择，支持运行期 fallback 热替换；策略可配置（上层可强制软/硬解）。
3. **帧处理链可插拔（IFrameSink 链式插入）**：processor 同时实现 IFrameSink（接上游）+ 持有下游 sink（转发），credit 回执原样透传即保持流控不变。sonic 音频变速是第一个内置范例；未来视频滤镜、音效、录制分流（tee）走同一插入点。不实现通用 pipeline 框架，避免过度设计。
4. **渲染后端可插拔（IVideoRender/IAudioRender，接口已就位）**：GLES/未来 Vulkan、SLES/AAudio 均为策略实现，按系统版本或配置选择。

事件通道纪律：新增事件一律经组件 `postNotify(type, event, args)` → `VEPlayer::onComponentEvent` → Java 回调链路，事件号在 VEDef.h 集中分配；禁止组件间直接互调或另起线程回调。

---

# Phase 0：地基（预备 bugfix 批次 + 等价重构）

分两个严格隔离的批次，各自独立 commit、独立真机回归，保证基线清晰。

## 0.A 预备 bugfix 批次（行为修正，先行）

来源：2026-08-07 pipeline review 的问题清单。本批只收"改动小、风险低、立刻受益"的项，其余归入对应 Phase。

| review 问题 | 修法 | 行为变化 |
|------|------|----------|
| SLES `configure` 成功返回 `VE_UNKNOWN_ERROR`（VEAudioSLESRender.cpp:169）；`start` 已播放时返回错误 | 返回 `VE_OK`；start 幂等返回 VE_OK | 无（当前被掩盖） |
| 音频渲染硬错误静默死链（VEAudioRender.cpp:267） | `onRender` 非 WOULD_BLOCK 错误时 `postMessage(VE_NOTIFY_EVENT_ERROR)`，交 VEPlayer 错误收敛 | 有：故障从静默卡死变为上报 ERROR（预期改善） |
| `VEAudioDecoder::onPrepare` 返回 `false`/`-1` 混用、open 失败泄漏 ctx | 对齐 VEVideoDecoder 写法：规范 VEResult + 每步失败清理 | 无 |
| 热路径日志：`VEFrame` 析构 ALOGI（每秒 ~150 条）、解码器 per-packet ALOGI | 降为 ALOGV（编译期宏裁剪在 Phase 4 统一做） | 无（日志量下降） |
| shader 色彩量程：limited-range YUV 未做 `1.164*(y-16/255)` 展开，画面发灰 | 按 `frame->color_range` 选 full/limited 系数（uniform 开关）；BT.709 系数一并支持（按 colorspace 选） | 有：画面对比度恢复正常（预期改善，回归时人工确认观感） |
| 单坏包打死播放（两个 Decoder 的 send_packet 错误零容忍） | `AVERROR_INVALIDDATA` 跳包继续 + 连续计数上限（对齐 demux 的 100 上限策略），其余错误仍致命；顺带修正 VEVideoDecoder 返回 `VE_ERROR_EAGAIN` 的语义误用 | 有：损坏文件从报错停止变为跳包续播 |

**不在本批**：旋转支持（依赖轨道模型的元数据扩展，归 0.B/Phase 2）；音频时钟精度（归 Phase 4 AAudio）；重采样双拷贝（归 Phase 1 顺带，见 1.1）。

验收：构建过；真机回归基线（播放/暂停/seek/EOS）+ 上表三项"预期改善"逐项人工确认。

## 0.B 等价重构（无行为变化，多音轨选流除外——见 0.B.4）

### 0.B.1 IVEComponent + Role 表

```cpp
// interface/IVEComponent.h
class IVEComponent {
public:
    virtual ~IVEComponent() = default;
    virtual VEResult start() = 0;
    virtual VEResult stop() = 0;
    virtual VEResult pause() = 0;
    virtual VEResult seekTo(double timestampMs) = 0;
    virtual VEResult flush() = 0;
    virtual VEResult release() = 0;
};
```

现有五组件（VESource 已含同名方法，签名统一即可；两解码器、两渲染器补 override 声明）全部实现。VEPlayer 改造：

```cpp
enum RoleIndex { ROLE_SOURCE, ROLE_ADEC, ROLE_VDEC, ROLE_ARENDER, ROLE_VDISPLAY,
                 ROLE_SUBTITLE /*Phase 5 启用*/, kRoleCount };
struct Role {
    std::shared_ptr<IVEComponent> comp;   // null = 该角色不存在
    std::shared_ptr<ALooper>      looper;
    RoleState                     state = ROLE_NONE;
};
std::array<Role, kRoleCount> mRoles;
```

- 9 处"五个 if 挨个调组件"（onStart/onStop/onPause/converge/seekStagePause/seekStageSeek/seekFinish/teardownComponents/enterTeardownReleaseStage）改为 `for (auto &r : mRoles) if (r.comp) { r.comp->xxx(); r.state = ...; }`。
- `roleStateFor(componentType)` 改为 componentType → RoleIndex 映射表；`rolesAllIn`/`setAllRoles`/`anyRoleExists` 改数组遍历。
- `finishTeardown` 的 looper/handler 成对数组直接由 mRoles 派生。
- **注意**：具体类型成员（`mAudioDecoder` 等）保留为 Role 表的强类型别名引用（或访问器），prepare 建链、audio 专属调用（如传 outConfig）仍需具体类型；Role 表只服务命令扇出与状态机。

### 0.B.2 IMediaDecoder

```cpp
// interface/IMediaDecoder.h
class IMediaDecoder : public IVEComponent {
public:
    /// params: 音频解码器经 VEBundle 取 outSampleRate/outChannels/outFormat；视频忽略
    virtual VEResult prepare(std::shared_ptr<IMediaSource> source,
                             std::shared_ptr<IFrameSink> sink,
                             const VEBundle &params) = 0;
};
```

VEAudioDecoder/VEVideoDecoder 实现之。VEPlayer::continuePrepare 中解码器局部变量类型改为 IMediaDecoder，为 Phase 2 工厂替换做准备。

### 0.B.3 Source scheme 注册表

```cpp
// core/VESourceRegistry.h
using SourceFactory =
    std::function<std::shared_ptr<VESource>(std::shared_ptr<AMessage> &notify)>;
class VESourceRegistry {
public:
    static VESourceRegistry &instance();
    void registerScheme(const std::string &scheme, SourceFactory f); // "file"/"http"/"https"
    std::shared_ptr<VESource> create(const std::string &path,
                                     std::shared_ptr<AMessage> &notify); // 无 scheme 视为 file
};
```

`VEPlayer::createSource` 改为查注册表。默认注册 file→VEDemux（进程内一次性注册，静态初始化或 JNI_OnLoad）。

### 0.B.4 轨道模型（Phase 5 的地基，也修 review #5/#10）

```cpp
// core/VEMediaDef.h
enum class ETrackType { AUDIO, VIDEO, SUBTITLE };
struct VETrackInfo {
    int         index;        // 播放器轨道号（外挂字幕用 >= 0x10000 的虚拟号）
    int         streamIndex;  // FFmpeg stream index（虚拟轨为 -1）
    ETrackType  type;
    AVCodecID   codecId;
    std::string lang, title;  // 取自 AVDictionary "language"/"title"
    AVCodecParameters *codecParams; // VEMediaInfo 深拷贝自持，析构统一释放
    AVRational  timeBase;
    int64_t     startTime;
    int         rotationDegrees; // 视频轨：AV_PKT_DATA_DISPLAYMATRIX 解出，0/90/180/270
};
struct VEMediaInfo { std::vector<VETrackInfo> tracks;
                     int activeAudio = -1, activeVideo = -1, activeSubtitle = -1;
                     /* duration/fps/width/height/sampleRate... 保留 */ };
```

- **codecParams 由 VEMediaInfo 深拷贝自持、析构释放**——消除 review #10 的悬垂指针（demux onRelease 不再负责这些拷贝的生命周期）。
- `IMediaSource::read(bool isAudio, ...)` → `read(ETrackType, ...)`；VEDemux 内部包队列改按类型三条（字幕队列 Phase 5 启用，容量 64，**不参与** shouldParkRead 判据——字幕包稀疏，不能拖读循环）。
- 默认选流改 `av_find_best_stream(AVMEDIA_TYPE_AUDIO/VIDEO)`——**这是本批唯一预期行为变化**（多音轨文件默认轨从"最后一条"变为"最佳轨"），基线回归单独标注。
- 视频轨旋转角从 stream side data（`AV_PKT_DATA_DISPLAYMATRIX`，`av_display_rotation_get`）解出存入 `rotationDegrees`；GLES 渲染器 `calculateTransformMatrix` 按其出旋转矩阵（修 review #3，软解路径立即生效；硬解路径 Phase 2 经 AMediaFormat "rotation-degrees" 处理）。

### 0.B.5 清理与文档

- 删除死文件：VEFrameQueue.{h,cpp}、VERingBuffer.h、VEAudioTrack.{h,cpp}、VEMediaCodecDecoder.{h,cpp}（均无引用，CMakeLists 未编译）。
- 删除两渲染器残留的注释 dump 代码与 `fp` 成员。
- 刷新 ARCHITECTURE.md：组件命名（VEVideoDisplay）、新接口、推模型 + credit 数据流、线程模型、本方案各 Phase 的演进路线。

### 0.B 验收

构建过；除 0.B.4 选流一项外与 0.A 后基线行为完全一致；真机回归全绿后才进 Phase 1。

---

# Phase 1：变速 + 精准 seek 提速

## 1.1 VESonicProcessor 与音频链改造

**插入点**：VEAudioRender::onRender 内、`renderFrame(设备)` 之前（不是独立 AHandler——变速处理无阻塞、无自有线程需求，作为 render 的内联处理级，符合"IFrameSink 链式插入"思想的最简形态）。

```cpp
// core/VESonicProcessor.h（包装 sonic_stream）
class VESonicProcessor {
public:
    VEResult configure(int sampleRate, int channels);   // sonicCreateStream
    void setSpeed(float speed);                          // sonicSetSpeed，pitch 保持 1.0
    /// 写入一帧 S16 交织 PCM（sonicWriteShortToStream）
    void write(const uint8_t *pcm, int nbSamples);
    /// 读出尽可能多的已变速样本到 staging 缓冲，返回样本数（sonicReadShortFromStream）
    int  read(uint8_t *out, int maxSamples);
    /// flush 内部滞留（seek/变速切换/EOS 收尾时）
    void flush();                                        // sonicFlushStream + 清记账
    int64_t pendingInputSamples() const;                 // 滞留在 sonic 内的输入样本数
};
```

**数据流（speed != 1.0 时）**：`mFrames` 队首帧 → `write()` → 立即投 consumedReply（credit 在"帧被吞入 processor"时归还；解码侧在途量语义不变）→ `read()` 到 staging 缓冲攒满一个设备块（沿用 20ms 粒度）→ `renderFrame(staging)` → 设备。speed == 1.0 时旁路 processor，零开销。

**顺带修 review #8（重采样双拷贝）**：VEAudioDecoder 重采样路径改为 `swr_convert` 直接输出到目标 VEFrame 的 buffer（先 `av_frame_get_buffer` 按 `swr_get_out_samples` 上界分配，转换后修正 `nb_samples`），删除 `av_samples_alloc_array_and_samples` 临时缓冲与 memcpy。

## 1.2 pts 记账与时钟公式

设 flush（seek/变速切换/起播）后：`P0` = 首个写入帧的 pts（µs），`sr` = 输出采样率，`Out` = 累计从 sonic 读出的样本数，`speed` = 当前速率。则：

- 当前已送设备的媒体位置：`M = P0 + Out / sr * 1e6 * speed`
- 设备侧补偿（媒体时长）：`C = (设备在途实时时长 + kDeviceOutputLatencyUs) * speed`
- 时钟锚点：`updateAudioPts(M - C)`

speed 在两次 flush 之间恒定（见 1.3 切换即 flush），公式无跨速率混算问题。1.0x 旁路时退化为现有逻辑（M = 帧 pts，speed=1）。

`getQueuedDurationUs()`（SLES 实现）同批改为按各在途缓冲真实样本数累加（帧引用队列里存 nb_samples），替换"帧数 × 20ms"估算（review #9 的第一步；根治在 Phase 4 AAudio getTimestamp）。

## 1.3 setPlaySpeed 全链路与切换时序

- 链路：Java `setPlaySpeed(float)` → JNI `nativeSetPlaySpeed`（已有）→ Driver `setSpeedRate`（已有）→ `VEPlayer::setPlaySpeed`：投 `kWhatSetSpeed` 消息（新增），去掉现在的"直接返回不支持"。
- `onSetSpeed`（player 线程）：
  1. 范围钳制 [0.5, 2.0]，超范围返回 `VE_INVALID_PARAMS`；
  2. `isFlowBusy()` 时入 PendingAction（新增 `ACTION_SET_SPEED`，与队尾同类型合并，同 seek 的合并策略）；
  3. 执行：`mMediaClock->setPlaybackSpeed(speed)`（已实现，内部先按旧速结算）→ 音频链存在时 `mAudioOutput->setSpeed(speed)`（新增命令消息，render 线程内：sonic setSpeed + processor flush + 设备缓冲 flush[SLES Clear + 预填静音，复用 seek 路径] + 记账 rebase：`P0 = mMediaClock->getCurrentMediaTime()`）；
  4. 纯视频文件只改时钟。
- 变速与 seek/暂停交互：seek 流程不动 speed；暂停时设速仅更新时钟与 sonic 参数（无数据在流动）；变速中 seek 正常入流程（seek 的 flush 自然清 processor）。

## 1.4 AVSync 速率折算

`VEAVsync::getWaitTime()` 返回的是媒体时间差，但 `AMessage::post(delay)` 等待的是真实时间。时钟以 speed 倍外推 ⇒ 媒体差按 speed 倍速收敛：

- `waitReal = diff / speed`（`speed = mMediaClock->getPlaybackSpeed()`，VEMediaClock 补一个读接口）
- 失信兜底路径（`diff > kMaxDiffUs` 按帧率出帧）不折算——那是实时节奏。

## 1.5 精准 seek 提速（skip_frame）

- `VEVideoDecoder::onSeek`：`onFlush()` 后设 `mVideoCtx->skip_frame = AVDISCARD_NONREF`；
- `onDecode` 中到达 target（现有 `mSeekTargetUs` 清除点）时恢复 `AVDISCARD_DEFAULT`；`onStop/onFlush`（非 seek 路径）兜底恢复。
- 关键帧是参考帧，NONREF 不会跳过它，安全；B 帧密集内容收益最大；全 I 帧流无收益无害。
- 音频路径不做（音频解码本身廉价，丢帧逻辑已够快）。

## 1.6 文件改动清单

| 文件 | 改动 |
|------|------|
| core/VESonicProcessor.{h,cpp} | 新增 |
| core/VEAudioRender.{h,cpp} | processor 集成、setSpeed 命令、EOS 时 processor flush 排空 |
| core/VEPlayer.{h,cpp} | kWhatSetSpeed/onSetSpeed、ACTION_SET_SPEED |
| core/VEMediaClock.{h,cpp} | getPlaybackSpeed() |
| core/VEAVsync.{h,cpp} | getWaitTime 速率折算 |
| core/VEAudioDecoder.cpp | 重采样直写目标帧（去双拷贝） |
| core/VEVideoDecoder.cpp | skip_frame 设置/恢复 |
| platform/.../VEAudioSLESRender.{h,cpp} | getQueuedDurationUs 按真实样本数 |
| CMakeLists.txt | sonic 头/链接确认（已链接） |

## 1.7 验收

- 0.5/0.75/1.25/1.5/2.0x：音调不变（听感）、进度条速率正确、音画同步无可感偏差；
- 变速状态下 seek、暂停/恢复、EOS、循环播放全部正常；变速切换瞬间无爆音（flush 路径验证）；
- 长 GOP 素材（GOP≥120 的 1080p H.264）seek 追赶耗时 before/after 数据，预期显著下降；
- 真机回归基线全绿。

---

# Phase 2：MediaCodec 硬解 + Surface 零拷贝

## 2.1 组件形态：硬解组件 = 解码 + 同步 + 上屏合一

MediaCodec 直出 Surface 时，"帧"是 output buffer index，不能穿越 IFrameSink 传给 VEVideoDisplay（release 必须在 codec 上做）。因此 **VEMediaCodecVideoDecoder 一个组件承担视频解码 + AVSync 调度 + 上屏**：

- 在 Role 表中**同时占据 ROLE_VDEC 与 ROLE_VDISPLAY 两个角色位**（同一 comp 指针注册两次），对上回执两份 ack（type 分别为 VIDEO_DECODER / VIDEO_RENDER）。VEPlayer 的 seek 三阶段、teardown 两阶段状态机**完全无需感知软/硬解差异**（seek priming 等 VIDEO_RENDER 的 FIRST_FRAME，由本组件发出）。
- 软解链路（VEVideoDecoder + VEVideoDisplay + GLES）完整保留，作为 fallback 与 API < 28 的默认路径。

## 2.2 版本门槛与线程模型

- NDK `AMediaCodec_setAsyncNotifyCallback` 需 **API 28+**：API ≥ 28 走异步硬解；**API 24~27 保持软解**（占比低，不为其做同步轮询模式；未来有需求再补）。
- 异步回调（codec 内部线程）只做投递：`onAsyncInputAvailable`→`kWhatCodecInput{index}`、`onAsyncOutputAvailable`→`kWhatCodecOutput{index,pts,flags}`、`onAsyncFormatChanged`、`onAsyncError`→组件自身 looper（复用现 vdec_thread）。全部状态只在 looper 线程变化，epoch 防过期沿用。

## 2.3 输入路径

- **码流格式**：MP4/MKV 中 H.264/H.265 为 AVCC/HVCC（长度前缀），MediaCodec 要 Annex-B。经 FFmpeg bsf `h264_mp4toannexb` / `hevc_mp4toannexb`（`av_bsf_*` API）逐包转换；TS/FLV 已是 Annex-B 时 bsf 自动透传。
- **csd**：从 `AVCodecParameters::extradata`（avcC/hvcC）解析参数集转 Annex-B，填 `AMediaFormat` csd-0（SPS）/csd-1（PPS；HEVC 合并 VPS/SPS/PPS 进 csd-0）。
- **rotation**：`AMediaFormat_setInt32(format, "rotation-degrees", track.rotationDegrees)`（配合 Surface 输出由系统合成处理）。
- 输入循环：`kWhatCodecInput` 到达 → `mSource->read(VIDEO)` → EAGAIN 则挂起该 index 到空闲池，starve 重试沿用 10ms 轮询（Phase 4 统一改通知）→ bsf → `AMediaCodec_queueInputBuffer(index, pts)`；EOF 包 → `BUFFER_FLAG_END_OF_STREAM`。

## 2.4 输出路径与同步

- `kWhatCodecOutput` 到达 → 入本地待渲染队列（index+pts）→ 复用现有 VEAVsync 判定：
  - `shouldDropFrame()` → `AMediaCodec_releaseOutputBuffer(index, false)` 丢帧追赶；
  - 否则 `post(kWhatRelease, waitTime/speed)` 到点 `releaseOutputBuffer(index, true)` 上屏。
- 上屏后逻辑对齐 VEVideoDisplay：首帧发 `FIRST_FRAME`（seek priming 判据）、EOS flag 发 `EOS`、进度由 VEPlayer tick 统一（不逐帧上报）。
- 待渲染队列深度即天然 credit（codec output buffer 数量有限，无需另设计数）。

## 2.5 seek / flush / surface 生命周期

- seekTo：`AMediaCodec_flush`（异步模式 flush 后必须 `AMediaCodec_start` 重新拉起回调）+ 清待渲染队列 + epoch++ + 记 mSeekTargetUs（release 前按 pts 丢弃 target 之前的输出 buffer——硬解的精准 seek 等价物；无需 skip_frame）。
- surface 变化：`AMediaCodec_setOutputSurface`（API 23+，异步模式可用）；surface 置空期间停止 render=true 的 release（改 false 丢弃），恢复后续播——对齐现有 m_SurfaceLost 语义。
- release：`AMediaCodec_stop` + `AMediaCodec_delete` 在自身 looper 上，回执 RELEASE_DONE ×2（两个角色位）。

## 2.6 工厂与 fallback

```cpp
// core/VEVideoDecoderFactory.h
struct DecoderPolicy { bool forceSoftware = false; bool forceHardware = false; };
std::shared_ptr<IMediaDecoder> createVideoDecoder(
        const VETrackInfo &track, ANativeWindow *surface,
        const DecoderPolicy &policy, std::shared_ptr<AMessage> &notify);
```

- 选择条件：API ≥ 28 且 codec ∈ {H264, HEVC}（首批白名单）且 surface 就绪且未 forceSoftware → 硬解；否则软解。
- **创建期 fallback**：`AMediaCodec_createDecoderByType`/configure/start 任一失败 → 工厂内直接改建软解，仅打日志。
- **运行期 fallback**：`onAsyncError`/持续解码错误 → 组件 postNotify `VE_NOTIFY_EVENT_ERROR`，arg2 标记 `VE_INFO_DECODER_FALLBACK` → VEPlayer 错误收敛路径上新增分支：不进 STATE_ERROR，而是执行内部重建流程（新增 `ACTION_REBUILD_VIDEO`：teardown 视频两角色 → 以 forceSoftware 策略重建 → 精准 seek 到当前位置恢复），并 notify `VE_INFO_DECODER_FALLBACK` 告知上层。重建流程复用 PendingAction 串行化，与 seek/切轨天然互斥。
- fallback 注入测试：Debug 构建暴露 `DecoderPolicy` 强制开关 + 用不在白名单的 codec 素材（如 VP9/AV1）验证工厂路由。

## 2.7 验收

- 1080p30/1080p60 H.264、1080p HEVC：硬解生效（logcat 确认路由），simpleperf 整机 CPU before/after，目标 < 15%（1080p30）；
- 播放/暂停/seek（精准）/EOS/循环/变速/切后台恢复全回归；
- fallback：创建期（白名单外素材）与运行期（强制注入）两条路径均回到软解且播放连续；
- 与 Phase 1 变速叠加验证（硬解 + sonic）。

---

# Phase 3：网络源

## 3.1 分层

```
VENetworkSource (VESource, demux 内核复用)
    └── AVIOContext (read/seek 回调)
          └── VEBufferedDataSource   预取线程 + 环形缓存 + 水位
                └── VEHttpDataSource  HTTP/1.1 客户端 (socket + openssl TLS, Range)
                        (IDataSource: open/read/readAt/size/abort/close)
```

```cpp
// core/net/IDataSource.h
class IDataSource {
public:
    virtual VEResult open(const std::string &url, int64_t offset) = 0;
    virtual ssize_t  readAt(int64_t offset, void *buf, size_t size) = 0; // 阻塞，abort 可中断
    virtual int64_t  size() const = 0;      // Content-Length，未知 -1
    virtual void     abort() = 0;           // 任意线程，关 socket 解阻塞
    virtual void     close() = 0;
};
```

## 3.2 VEDemux 拆分方式

`VEDemux::onPrepare` 参数化打开路径：新增受保护虚函数 `openInput(AVFormatContext *ctx, const std::string &path)`——基类实现 `avformat_open_input(url)`（本地，现状不变）；`VENetworkSource : VEDemux` 覆写为 `avio_alloc_context(readCb, seekCb, opaque=mBufferedSource)` 挂到 `ctx->pb` 再 open。demux 内核（读循环/节流/seek/队列/命令面）零改动复用。interrupt_callback 语义保留：`abort()` 同时置 FFmpeg 中断标志并调 `IDataSource::abort()`。

## 3.3 VEHttpDataSource（native 自实现）

- HTTP/1.1 + TLS（openssl，已链接）：`Range: bytes=off-`、跟随 301/302/307（上限 5 跳）、解析 Content-Length/Content-Range、连接复用（keep-alive，断了重连）。
- 超时与重试：connect 5s / 首字节 5s / read 10s；瞬时错误指数退避重试 3 次；超限 → `VE_PLAYER_ERROR_NETWORK_TIMEOUT` / `VE_PLAYER_ERROR_NETWORK_IO` 经 postNotify 上报。
- abort：shutdown+close socket，阻塞 read 立即返回错误；openssl BIO 同步中断。
- 明确不做（登记未来项）：HTTP/2、代理、cookie、自定义 header 注入（接口预留 map 参数）、HLS/DASH。

## 3.4 VEBufferedDataSource（预取 + 环形缓存 + 水位）

- 独立预取线程（普通 std::thread，非 looper）：从 `mReadOffset` 连续下载填环形缓存（默认 32MB，可配）；缓存满即歇，消费腾出即续。
- `readAt(offset, ...)`：
  - 命中窗口 `[cacheStart, cacheEnd)` → 拷出返回；
  - 前向未达（offset ≥ cacheEnd）且距离 < `kForwardSkipMax`（默认 512KB）→ 等预取赶到；
  - 其余（后向或远前向）→ **重定位**：停预取、清缓存、`VEHttpDataSource::open(url, offset)` 重开 Range、重启预取。
- 水位与事件（唯一判定点在此层）：
  - 起播水位：`prepare` 后缓冲达 `startWaterBytes`（按码率估算 ~2s，默认 2MB）才回 PREPARE_DONE 之后的首次可读；
  - 低水位：消费到 `cacheEnd - readOffset < lowWaterBytes`（默认 256KB）且未 EOF → postNotify `BUFFERING_START`，之后每 500ms 报 `BUFFERING_UPDATE(percent)`；
  - 恢复水位：缓冲重新达 `resumeWaterBytes`（默认 1MB）→ `BUFFERING_END`。
- demux 侧节流参数网络化：`kMaxTotalBytes`/`kBufferedDurationTargetUs` 改为构造参数，VENetworkSource 传大值（64MB / 10s，可配）。

## 3.5 卡顿与 VEPlayer 的交互（内部缓冲暂停）

新增内部流程（不改对外状态机）：

- `BUFFERING_START` 且 `mState == STATE_STARTED` 且 `!isFlowBusy()`：置 `mBuffering = true`，fire-and-forget 暂停时钟 + 数据面组件（复用 converge 的停法但不改 mState、不清 seek 状态），事件继续透传 Java；
- `BUFFERING_END`：若 `mBuffering && mState == STATE_STARTED` → 恢复（组件 start + 时钟 resume）；用户在缓冲期间 pause 过（mState 已变 PAUSED）→ 只清 mBuffering 不恢复；
- `isFlowBusy()`（seek/teardown 在途）时收到缓冲事件：只透传 Java 不动数据面——seek 流程自身会重建数据流，teardown 不需要。

## 3.6 验收

- 局域网 HTTP 服务器（可限速）播放 MP4/MKV/FLV/TS：起播、seek（缓存内滑动 + 跨缓存 Range 重定位两种路径）、EOS；
- 限速至低于码率：BUFFERING_START/UPDATE/END 事件序列正确、画面停在最后一帧、恢复后音画同步；
- 断网：超时错误上报、ERROR 态可 reset 重来；播放中 release/reset 在 3s 内有界返回（teardown 硬指标）；
- 缓冲期间 pause/seek/切后台组合操作无状态机错乱。

---

# Phase 4：能耗与调度精修

## 4.1 饥饿轮询改登记-通知

```cpp
// IMediaSource 新增
/// read 返回 VE_NOT_ENOUGH_DATA 后，调用方可登记一次性通知；
/// 对应轨道有新数据入队时 post 一次并清除登记（one-shot，防通知风暴）
virtual void requestReadNotify(ETrackType type,
                               const std::shared_ptr<AMessage> &notify) = 0;
```

- VEDemux：per-track 原子槽存 notify（exchange 语义），`putPacket` 后取走并 post；
- 解码器：starve 时 `requestReadNotify(type, kWhatDecode 消息含当前 epoch)` 替换 `postDecode(10ms)`；epoch 守卫天然作废 flush 前的登记；
- 保底：登记后 500ms 无通知的兜底重试（防源实现遗漏通知导致永久饥饿）。

## 4.2 AAudio 渲染后端

- `VEAAudioRender : IAudioRender`：`AAudioStreamBuilder`，`AAUDIO_PERFORMANCE_MODE_LOW_LATENCY`，data callback 拉模式——callback 从内部 PCM 环形缓冲取数，`VEAudioRender::onRender` 改为向环形缓冲写（WOULD_BLOCK 语义保留）；
- 时钟根治（review #9）：`AAudioStream_getTimestamp` 得到"已呈现帧数 + 时刻"，`getQueuedDurationUs` 用真实呈现位置替代估算，删除 40ms 常数；
- 工厂选择：API ≥ 26 → AAudio，否则 SLES；Debug 可强制切换；
- error callback（设备断开等）→ postNotify ERROR 走收敛。

## 4.3 日志与线程

- 新增 `VE_LOG_HOT` 宏：热路径（per-packet/per-frame）日志统一挂宏，release 构建编译期裁除；
- 可选项（默认不做，先测后决）：音频解码与渲染合并 looper。

## 4.4 测量方法（每阶段通用，Phase 4 定稿模板）

- `simpleperf stat -p <pid> --duration 60`（整机 CPU%、每线程占比）+ `simpleperf record` 火焰图存档 test-reports/；
- 对照组固定：同一设备、同一素材（1080p30 H.264 基准片）、屏幕常亮、飞行模式（本地）；
- 指标表模板：{阶段, 场景, CPU%, 主要线程分布, 唤醒次数/秒(dumpsys/trace), 备注}。

---

# Phase 5：多轨与字幕

对齐 NuPlayer/MediaPlayer 的 getTrackInfo/selectTrack 语义。`kWhatClosedCaptionNotify` 占位由本阶段落地替换。

## 5.1 轨道 API 全链路

- JNI 新增：`nativeGetTrackInfo(handle): String`（JSON 数组，字段 index/type/lang/title/codec）、`nativeSelectTrack(handle, int)`、`nativeDeselectTrack(handle, int)`、`nativeAddExternalSubtitle(handle, String)`；
- Driver 状态校验：PREPARED/STARTED/PAUSED/PLAYBACK_COMPLETE 可调，其余返回错误；
- Java `VEPlayer.getTrackInfo()` 解析 JSON 为 `TrackInfo[]`（org.json）。

## 5.2 音轨切换流程

**设计修正说明**：初版方案设想"只动音频链、视频链完全不动"。细化时确认：单 AVFormatContext 下 demux 读位置全局唯一，切轨必须把读位置拉回当前播放位置，重读区间内的视频包无法凭空跳过（不重定位则新音轨从"读位置"而非"播放位置"开始，缺一段缓冲深度的音频）。因此：

**切轨 = demux 换活跃轨 + 全链精准 seek 到 currentMediaTime**。视频链会 flush 并重解当前 GOP——渲染器不清屏，画面表现为**短暂定格（非黑屏）**；硬解 + skip_frame（Phase 1/2 成果）下定格 < 200ms。真正无缝切换（双 AVFormatContext 预热新轨）登记为未来优化项，不在本期。

流程（`ACTION_SELECT_TRACK` 纳入 PendingAction，与 seek/reset/变速互斥串行）：

1. `onSelectTrack(index)`：校验 index 存在且类型为 AUDIO/SUBTITLE（视频轨切换不支持，返回 `VE_PLAYER_ERROR_UNSUPPORTED_TRACK`）；字幕轨走 5.4 轻量路径；
2. 音轨：记录 `target = mMediaClock->getCurrentMediaTime()`；
3. 若新旧轨 codec/参数不同：teardown 音频两角色（复用 teardown 阶段化握手，仅音频子集）→ 按新轨 `VETrackInfo` 重建 VEAudioDecoder + 输出配置（`chooseAudioOutputConfig` 按新轨参数重算，设备参数变化时 VEAudioRender 重 prepare）；相同 codec：只 flush；
4. `mSource->selectTrack(index)`（demux 命令：更新 activeAudio、清三队列）+ 全链 `startSeek(target)`（复用完整 seek 三阶段，含首帧 priming）；
5. seekFinish 后 postNotify `TRACK_CHANGED(index)`。

时钟：seek 流程本身 resetTo(target)，新轨首帧 updateAudioPts 重新锚定，无额外处理。

## 5.3 VESubtitleTrack 组件

- AHandler，注册 Role 表 `ROLE_SUBTITLE`（独立 looper "subtitle_thread"；包稀疏，负载可忽略）；实现 IVEComponent 全命令面 + 回执（PAUSE_DONE/STOP_DONE/…type=E_COMPONENT_TYPE_SUBTITLE），VEPlayer 状态机零改动接纳（Role 表遍历自动覆盖）。
- 数据链（不走 IFrameSink/credit——cue 稀疏无需流控）：
  - `kWhatFetch` 循环：`mSource->read(SUBTITLE)` → 空则 `requestReadNotify`（Phase 4 前用 200ms 轮询，字幕不敏感）→ `avcodec_decode_subtitle2`（字幕专用 API）→ 得 `AVSubtitle`；
  - cue 提取：`SUBTITLE_ASS` rect 解析 Dialogue 行取 Text 字段并剥 `{\...}` 样式标签；`SUBTITLE_TEXT` 直取；`pts + start/end_display_time` 折算 `{startUs, endUs, text}` 入 cue 有序队列；
  - 调度（一次只挂一个定时器）：取队首 cue，`delayReal = (startUs - clock.now()) / speed`，`post(kWhatCueFire, delayReal)`；到点 postNotify `SUBTITLE(text)`，再挂 `endUs` 的 `kWhatCueClear` → postNotify `SUBTITLE_CLEAR`，继续下一条。消息带 epoch；
  - pause：停调度（epoch++）；resume：从 cue 队列按当前时钟重新挂（已过期 cue 丢弃）；seekTo：epoch++ 清 cue 队列 + `SUBTITLE_CLEAR` 兜底 + 重新 fetch；变速：收 setSpeed 通知后 epoch++ 重挂当前定时器（delayReal 重算）。
- Java 侧：app 模块 SubtitleView（TextView overlay，底部居中、描边样式），IVEPlayerListener 新增 onSubtitle/onSubtitleClear。

## 5.4 字幕轨切换 / 关闭 / 外挂

- `selectTrack(字幕轨)`：无 VESubtitleTrack 则创建注册（首次选择才建，默认不建）；`mSource->selectTrack` 切 activeSubtitle + 组件 flush（清 cue + CLEAR）+ 重 fetch。无解码器重建、无全链 seek，轻量路径；
- `deselectTrack`：组件 stop + `SUBTITLE_CLEAR`；activeSubtitle = -1（demux 丢弃字幕包）；
- `addExternalSubtitle(path)`：player 线程独立 `AVFormatContext` 打开 .srt/.ass → **一次性读完全部包解成 cue 列表**（字幕文件小，MB 级）→ 注册虚拟轨（index ≥ 0x10000，isExternal）→ 关闭 ctx。选中虚拟轨时 VESubtitleTrack 直接消费内存 cue 列表，不经 demux；时间轴直接用主时钟（外挂字幕自带绝对时间）。
- 位图字幕（PGS/DVB）不做，`getTrackInfo` 过滤不上报，登记未来项。

## 5.5 验收

- 素材：≥2 音轨 mkv/mp4（不同 codec 组合：AAC+AC3、AAC+AAC）、内嵌 SRT/ASS 的 mkv、外挂 .srt/.ass；
- 音轨切换：画面定格不黑屏、硬解 < 200ms（软解如实记录）、切换后音画同步、TRACK_CHANGED 上报；同 codec 与异 codec 两条路径；
- 字幕：显示/消失时刻与片源一致（±100ms）；暂停定格、seek 后立即匹配新位置、变速下节奏正确、EOS 清除；切轨/关闭即时生效；外挂加载与切换；
- 并发时序：切轨中 seek、seek 中切轨、切轨中 reset/release、缓冲中切轨（网络源），全部由 PendingAction 串行化保证无错乱。

---

# 依赖与顺序

- Phase 0（0.A → 0.B 顺序执行）是 1/2/3/5 的前置；
- Phase 1 独立且最小，先行拿到可感知能力；Phase 2 是 CPU 目标决定性一步（且大幅改善 Phase 5 切轨定格时长）；Phase 3 改动面最大放后；Phase 4 收尾精修；Phase 5 仅依赖 Phase 0，可按优先级灵活插排，默认最后（受益于 1/2 的 seek 提速）；
- 每阶段完成后 **lzplayer-test-expert** 真机回归（基线：播放/暂停/seek/EOS + 该阶段新能力），报告落 `test-reports/`。

# 测试素材清单（提前准备）

| 素材 | 用途 |
|------|------|
| 1080p30 / 1080p60 H.264 mp4（长 GOP ≥120） | CPU 基准、seek 提速对比 |
| 1080p HEVC mp4 | 硬解白名单第二 codec |
| VP9 / AV1 webm | 工厂路由 fallback（白名单外） |
| 纯音频 mp3/flac（含内嵌封面） | 纯音频链路回归 |
| 多音轨 mkv（AAC+AC3）、mp4（双 AAC） | 切轨 |
| 内嵌 SRT / ASS 字幕 mkv + 外挂 .srt/.ass | 字幕 |
| 局域网 HTTP 服务器 + 限速工具（如 nginx + tc） | 网络与弱网 |
| 损坏文件（截断/花包） | 坏包容忍回归 |

# 风险

1. **MediaCodec 厂商差异**（csd、flush 行为、低端芯片 codec 容量、rotation 处理）→ 软解 fallback 双路径（创建期/运行期）为验收硬条件；API 28 门槛把异步模式兼容面收窄。
2. **变速时钟连续性** → 切换即 flush + P0 rebase 公式（1.2/1.3），回归听爆音。
3. **网络阻塞 IO 与 teardown 时序** → interrupt_callback + IDataSource::abort 双通道解阻塞，release 有界为硬指标。
4. **Phase 0 基线纪律**：0.A（行为修正）与 0.B（等价重构）严格分批、各自回归；0.B 唯一预期行为变化（av_find_best_stream 选流）单独标注。
5. **切轨并发时序**：与 seek/reset/变速全部经 PendingAction 串行化，回归覆盖双向并发；"只动音频链"方案已修正为"全链 seek + 画面定格"（见 5.2 设计修正说明）。
6. **ASS 降级纯文本**：API 文档标注；样式保真（libass）登记未来项。
7. **sonic 极端参数**：0.5x 时输出样本量翻倍，staging 缓冲与设备块粒度按最大膨胀率预留，防止环形缓冲溢出。
