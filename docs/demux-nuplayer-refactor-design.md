# demux-nuplayer-refactor 设计文档（demux 驱动模式参考 NuPlayer 改造，阶段一）

> 创建日期: 2026-07-30
> 阶段范围: 阶段一（接口边界解耦）。阶段二（时间轴归一、网络 IO 剥离）单独立项，不在本 feature 内。

## 背景与目标（为什么）

当前 `VEPlayer` 直接持有具体类型 `VEDemux`，命令面（prepare/start/stop/pause/seekTo/flush/release/abort）通过具体类型指针调用，数据面（read/getFileInfo）则经 `IMediaSource` 接口向下传给解码器。这种"命令面绑死具体类型、数据面已抽象"的半解耦结构，阻碍了后续 source 的可插拔演进（本地文件 vs 网络流 RTMP/HLS）。

参考 NuPlayer 的分层：播放器只认一个抽象 `Source`，Source 内部再分具体实现。本阶段把这一边界先落下来——**只改接口边界，不改驱动模型**：

- `VEPlayer` 对 `VEDemux` 的具体类型持有，解耦为对 `VESource` 抽象接口的持有。
- `VEDemux` 从双继承 `AHandler, IMediaSource` 改为单继承 `VESource`（后者再继承 `AHandler` + `IMediaSource`）。
- 命令面方法全部上提到 `VESource` 抽象基类成为虚接口；数据面 read/getFileInfo 继承自 `IMediaSource`。
- 回执统一由 `VESource::postNotify` 发出，具体实现不再各自拼消息。

阶段一**不动**驱动模型内核（读循环自驱 + 消费侧拉取唤醒 + 命令面消息驱动回执），也**不动**时间轴归一与网络 IO 剥离（那是阶段二 / source-network-io 演进）。

## 驱动模型内核（务必保留，本方案不改）

### 数据流（读循环自驱 + 消费侧拉取唤醒）
- 读循环自驱：`onRead` 成功即 post `kWhatRead` 续读；`EAGAIN` 延时 10ms 续投；`shouldParkRead` 判定缓冲够则 park 不续投。
- 消费侧拉取唤醒：解码器 read 取包后 `scheduleContinueReadIfNeeded` 投 `kWhatContinueRead`，looper 收到再投 `kWhatRead` 拉起读循环。
- `mContinuePending` 原子去重，避免在途续读消息堆积。

### 控制流（命令面消息驱动）
- 命令投 `kWhatXxx` 消息进 source looper，回执经 `mNotify`(= `kWhatComponentEvent` + `plGen`) 回 `VEPlayer::onComponentEvent`，驱动建链后半段 / seek 状态机 / teardown 收敛。

### 数据面/命令面分离纪律
- `kWhatContinueRead` 只投 `kWhatRead`，**绝不**投命令消息。
- `mReleased` 终态防复活。
- `interrupt_callback` 解阻塞 `av_read_frame` / `avformat_open_input`。

## 技术方案（是什么 / 怎么做）

### 1. 新建 `interface/VESource.h` 抽象基类

```cpp
namespace VE {
    class VESource : public AHandler, public IMediaSource {
    public:
        explicit VESource(std::shared_ptr<AMessage> &notify);
        ~VESource() override = default;

        // 命令面（异步，回执经 postNotify）
        virtual VEResult prepareAsync(const std::string &path) = 0;
        virtual VEResult start() = 0;
        virtual VEResult stop() = 0;
        virtual VEResult pause() = 0;
        virtual VEResult seekTo(double timestamp) = 0;
        virtual VEResult flush() = 0;
        virtual VEResult release() = 0;
        virtual void     abort() = 0;   // 同步，可跨线程，解阻塞 FFmpeg IO

    protected:
        /// 统一回执：dup mNotify 后填 type=E_COMPONENT_TYPE_DEMUX + event + arg1/arg2/arg3/params。
        /// plGen 已由 notify 模板消息携带（VEPlayer 构造时 setInt32("plGen", ++mPipelineGen)），这里不重设。
        VEResult postNotify(int32_t event, int32_t arg1, int32_t arg2,
                            int64_t arg3, void *params);

        std::shared_ptr<AMessage> mNotify = nullptr;
    };
}
```

要点：
- 构造接收 `std::shared_ptr<AMessage> notify`，存为 `mNotify`。
- `postNotify` 复刻现 `VEDemux::postMessage` 的实现（dup → setInt32 type/event/arg1/arg2 → setInt64 arg3 → setPointer params → post），只是搬到基类。
- `plGen` **不在** `postNotify` 里设置——它由 VEPlayer 构造 notify 模板时写定，dup 时自动带过来。
- 命令面方法为纯虚，由 `VEDemux` override。

### 2. `core/VEDemux.h` / `.cpp` 改继承

- `class VEDemux : public VESource`（替换原 `AHandler, IMediaSource` 双继承）。
- 构造改为 `explicit VEDemux(std::shared_ptr<AMessage> &notify);`，实现里调 `VESource(notify)` 存 `mNotify`，原 `mNotifyEvent` 字段删除，内部所有 `mNotifyEvent->` 改 `mNotify->`。
- `prepare` 改名 `prepareAsync`（语义不变，仍异步：投 `kWhatPrepare`，完成后回 `VE_NOTIFY_EVENT_PREPARE_DONE(arg1=结果)`）。
- `start/stop/pause/seekTo/flush/release/abort` 全部加 `override`。
- `read` / `getFileInfo` 加 `override`（继承自 `IMediaSource`，经 `VESource`）。
- `onMessageReceived` 加 `override`（继承自 `AHandler`，经 `VESource`）。
- `postMessage` 删除，全部调用点改 `postNotify(...)`（实现上移到 `VESource`，签名一致）。
- 内部 `onMessageReceived`、读循环、`shouldParkRead` 节流、`kWhatContinueRead`、`interruptCallback`、`mReleased`、`scheduleContinueReadIfNeeded`、所有 `kWhatXxx` 枚举、所有成员变量**原样保留**。

### 3. `core/VEPlayer.h` / `.cpp` 改持有

- 成员 `std::shared_ptr<VEDemux> mDemux` → `std::shared_ptr<VESource> mSource`。
- 成员 `std::shared_ptr<ALooper> mDemuxLooper` → `std::shared_ptr<ALooper> mSourceLooper`。
- 新增工厂 `std::shared_ptr<VESource> createSource(const std::string &path)`：当前实现只 `return std::make_shared<VEDemux>(mRenderNotifyMsg);`，预留按 scheme 分支的位置（注释标注阶段二扩展点，不实现）。
- `setupDataSource` 里：`mDemuxLooper = make_shared<ALooper>` → `mSourceLooper`，`setName` 可保持 `"demux_thread"` 或改 `"source_thread"`（建议改，但不强求）；`mDemux = make_shared<VEDemux>(...)` → `mSource = createSource(mPath)`；`mDemuxLooper->registerHandler(mDemux)` → `mSourceLooper->registerHandler(mSource)`。
- `doPrepare` / `continuePrepare` / `start` / `stop` / `seek` / `pause` / `teardown` / `abort` 路径里所有 `mDemux->` 改 `mSource->`，`mDemuxLooper` 改 `mSourceLooper`，`mDemux != nullptr` 判空改 `mSource != nullptr`。
- `mDemux.reset()` / `mDemuxLooper.reset()` 改 `mSource.reset()` / `mSourceLooper.reset()`。
- 传给解码器的 `mDemux` 改 `std::static_pointer_cast<IMediaSource>(mSource)`（解码器仍只认 `IMediaSource`）。

### 4. `core/VEAudioDecoder.cpp` / `VEVideoDecoder.cpp`

- **零改动**。仍只认 `IMediaSource`，`prepare(std::shared_ptr<IMediaSource> source, ...)` 签名不变；VEPlayer 侧用 `std::static_pointer_cast<IMediaSource>(mSource)` 适配。

### 5. 事件分发

- `VEPlayer::onComponentEvent` 里 `E_COMPONENT_TYPE_DEMUX` 分支、`mSourceState` 角色映射、`plGen` 校验**全部不变**。`postNotify` 发出的消息字段与原 `postMessage` 完全一致，对消费侧透明。

## 涉及模块 / 文件

| 文件 | 改动 |
|------|------|
| `interface/VESource.h` | **新增**：抽象基类，命令面纯虚 + `postNotify` + `mNotify`。 |
| `core/VEDemux.h` | 改继承 `VESource`；`prepare`→`prepareAsync`；命令方法加 `override`；删 `postMessage` 声明与 `mNotifyEvent`。 |
| `core/VEDemux.cpp` | 构造调 `VESource(notify)`；`postMessage` 调用点改 `postNotify`，实现删除；其余内核原样保留。 |
| `core/VEPlayer.h` | `mDemux`→`mSource`（类型 `VESource`）；`mDemuxLooper`→`mSourceLooper`；新增 `createSource` 声明。 |
| `core/VEPlayer.cpp` | 新增 `createSource` 实现；全量 `mDemux`→`mSource`、`mDemuxLooper`→`mSourceLooper` 调用点改名；解码器 prepare 传 `static_pointer_cast<IMediaSource>`。 |
| `core/VEAudioDecoder.*` / `VEVideoDecoder.*` | 零改动。 |

## 风险与回归

- **编译风险**：`VESource` 多继承 `AHandler` + `IMediaSource`，与原 `VEDemux` 双继承等价，菱形无风险（两者均无虚继承冲突，`AHandler` 与 `IMediaSource` 独立）。需确认 `AHandler` 头文件 include 路径在 `interface/VESource.h` 可见。
- **行为风险**：`postNotify` 与原 `postMessage` 字段须逐字段对齐（type/event/arg1/arg2/arg3/params），否则 `onComponentEvent` 解析失败。`plGen` 必须仍由模板携带，不得在 `postNotify` 重设。
- **回归重点**（阶段一编译通过后上真机）：
  1. prepare 建链成功（音视频均起播）。
  2. start / pause / resume 顺畅。
  3. seek（含 flush + seekTo + SEEK_DONE 回执）状态机收敛。
  4. EOS 正常上抛。
  5. 换源 reset（teardown 后重新 prepare 新文件）。
- **明确不含**（阶段二 / 另行立项）：
  - 时间轴归一（`mStartTimeOffset` 逻辑搬迁）。
  - 网络 IO 剥离（source 按 scheme 分发、自定义 `AVIOContext` 注入）。

## 验收标准

- 每步 `./gradlew assembleDebug` 编译通过（零新增警告）。
- 真机回归（prepare/start/pause/seek/EOS/换源 reset）全绿，由 lzplayer-test-expert 跑全功能回归，报告落 `test-reports/`。
- 驱动模型内核代码（读循环、节流、续读去重、interrupt、mReleased）行级保持不变。
