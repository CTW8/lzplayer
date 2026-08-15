# 日志埋点整改与可观测性建设 设计文档

> Feature: observability-instrumentation
> 创建日期: 2026-08-15（补登记，实施已于同日全部完成）
> 提交范围: `d3b5e81..204a88a`，共 10 笔，已推送 `origin/master_openclaw`

## 1. 背景与起因

用户要求 review 现有日志埋点是否完善、能否完整评估播放器能力。review 结论是**日志密度与价值倒挂**：

- 12 秒播放打 **3533 行**日志，触发 ColorOS 每进程 300 行/秒配额 **11 次**，真正有用的日志被静默丢弃；
- 与此同时，`VEPlayerDriver` 的 **22 处状态迁移几乎不打日志**，`stop→play` 静默失败时无痕可查。

即：噪声占满了通道，信号根本没进通道。这两件事必须一起解决——只删日志不补状态机留痕，等于把可观测性从"淹没"变成"空白"。

另有一个结构性缺口（perf-metrics 设计稿 §8 已记）：**现有指标全是聚合快照，没有时间线**。p95 正常但某一秒全塌了，聚合值看不见。`docs/decoder-test-redesign.md` §5.2 要求的"逐秒时间线"是跑分报告的前置条件，此前未实现。

## 2. 目标

1. 把日志通道从"被每帧日志占满"改为"默认只留事件与每秒摘要"，且保留可随时打开的逃生开关。
2. 状态机全部迁移与守卫拒绝留痕，且**不可被配额挤掉、不可被 Release 剔除**。
3. 建立逐秒时间线，覆盖帧率 / 丢帧分类 / 队列水位 / 同步余量 / CPU / 渲染分解六个维度。
4. 建立**分段自校验**机制：每个环节都能回答"我的分段插桩覆盖了这条线程多少 CPU"，让插桩缺口自己暴露，而不是靠人去发现。

## 3. 技术方案

### 3.1 状态机留痕（`VEPlayerDriver`）

4 处守卫拒绝 + 18 处状态迁移改打 **ALOGW**。

级别选 W 的理由：它不是错误，但它是排查一切状态机问题的**唯一线索**。用 V/D/I 会被 300 行配额挤掉，也会在 Release 构建里被剔除，那时正是最需要它的时候。

### 3.2 每帧日志分档（`utils/Log.h` 新增 `ALOGF`）

此前只有两个极端：
- 默认构建 → 被每帧日志淹没；
- `VE_QUIET_LOG` → 把 V/D/I 一起编掉，**连事件日志也没了**。

中间缺的"少而有意义"这一档就是 `ALOGF`：默认关闭，`-PveTraceFrame=true` 打开（build.gradle + CMakeLists 传递）。

19 处每帧点位转入。**判据是打印频率而非重要性**——一次 seek/EOS/回退只打一条的，全部保留原级别。

另外单独处理了 progress 事件的三层重复：同一事件穿过 `VEPlayerDriver` → `native_PlayerInterface` → `NativeLib.postEventFromNative` 各打一遍，每秒两次共 6 行，12 秒里占 73 行。前两层转 `ALOGF`；**第三层不整条删**——它打的是所有事件，对状态变化、错误这类稀有事件很有价值，只是被 progress 淹掉了，所以只跳过 progress 一种。

实测：日志 3533 → **116 行**，配额丢弃 11 → **0 次**，播放进度与改前一致；`ALOGF` 逃生开关实测恢复 647 条每帧日志。

### 3.3 逐秒时间线（`VEPerfStats::Timeline`）

三条每秒日志行，固定前缀，`key=value` 空格分隔：

| 行 | 内容 | 说明 |
|----|------|------|
| `VESTAT` | 帧率、丢帧四类分解、队列水位（aq/vq/fq）、本秒最差同步余量、进程 CPU | 主行 |
| `VERENDER` | upload / draw / swap 三段 + 线程 CPU + gap | **仅软解有样本**；硬解走 `releaseOutputBuffer` 不经 GLES 渲染器，并进 VESTAT 的话那半行永远是哨兵 |
| `VEGAUGE` | 每条线程的 `cpu / instrumented / gap` 三元组 | 分段自校验，见 3.4 |

两条设计约束：

- **挂在已有的 `kWhatProgressTick` 上，不新增周期唤醒源。** 播放器已经为进度回调每秒醒来，可观测性不该再加一个。
- **累计量一律发差值，不发绝对值。** 从绝对值求差要求采样无丢失，而日志恰恰会被配额丢——这正是本 feature 要解决的问题，不能让新机制依赖它。

哨兵约定：**"没测到"统一用 `-9999`，不用 0。** 余量 0 是"即将开始丢帧"、偏移 0 是"完美同步"、CPU 0 对播放器是个值得警觉的读数——都是有意义的值，不能和"没测到"混同。轨道不存在的队列深度返回 `-1` 同理：无音轨报 0 会被读成"音频缓冲空了"，是完全不同的结论。

### 3.4 分段自校验（`VEPerfStats::CpuGauge` / `CpuScope`）

**通用件 `CpuGauge`**：`threadUs` 记线程累计 CPU，`instrumentedUs` 记插桩区间自身消耗的 CPU，每秒各取差值后相减即**窗口外开销**。`threadCpuUs()`（`CLOCK_THREAD_CPUTIME_ID`）从渲染器提到 `VEPerfStats.h` 供各处复用。

**`CpuScope`（RAII）**：构造记 CPU 起点，析构记账，**覆盖所有返回路径**。这一点是必需的——手工在若干出口各写一次记账，漏一条就静默失真（adec 的缺口正是 `receive_frame` 返回 EAGAIN 的提前返回路径，那些迭代照样烧 CPU 却从不走到记账点）。

接入四处：`vdec` / `adec` / `demux` / `video_render`。

**当前覆盖率**：vdec **99%**、adec **69%**、demux **68%**、video_render 窗口外约 **15%**。

demux 在接入之初 `instrumented` 恒为 0，机制**如实报出"这一环 100% 未插桩"而不是假装覆盖了**——这正是自校验该有的行为。

### 3.5 配套：基线素材生成脚本

`scripts/gen-test-assets.sh`（对应 `docs/decoder-test-redesign.md` §3 的素材矩阵）。

- **脚本入库而非素材入库**：4K/60fps 体积大（4K 10s 就 70MB），且素材参数一旦写死在二进制里，就没人知道它当初是怎么生成的。
- **合成源用 `testsrc2` 叠 `noise`**，不用纯色块或静止图——后者压缩后近乎为零、解码几乎不耗 CPU，测出来的数字会系统性低估真实负载。这个项目已经吃过一次亏：640×360 合成素材上 `find_stream_info` 只要 2~4ms，换 1080p 真实素材是 133~145ms，瓶颈排序整个变了。
- 生成后已用 ffprobe 逐个核对四个基线的分辨率/帧率/时长，noaudio 确无音轨，shortaudio 为视频 10s + 音频 3s。

## 4. 本轮最重要的经验（对后续开发有直接指导意义）

> 本节是本 feature 的核心产出，优先级高于上面任何一条实现细节。

### 4.1 七次同类错误，全部靠跑真机数据发现，没有一次是审代码发现的

本轮新增指标的第一版**错了七次**，七次是同一类问题：**字段名承诺 A、实际度量 B**。

| # | 字段 | 承诺 | 实际度量 |
|---|------|------|----------|
| 1 | 队列水位 | 瞬时深度 | "只涨不落"的整段峰值（实测 vq 从 37 单调爬到 67，看着像队列在涨，其实只是历史最大值被刷新） |
| 2 | 轨道存在性 | 该轨道存在 | "队列指针非空"——但三条队列在 prepare 时**无条件建好**，纯音频文件的视频队列同样非空 |
| 3 | `syncMargin` | 本秒同步状况 | 整段累计分位数（实测 25.5→22.5 的缓慢漂移，那是整段均值被新样本稀释） |
| 4 | `avOffMs` | A/V 偏移 | **距下一帧上屏还剩多久**——详见 4.2，最终整列删除 |
| 5 | CPU 软硬解对照 | 该配置的 CPU 水平 | 4 行样本的均值，而该列单次运行内方差达 **30 个百分点** |
| 6 | `gapMs` | 窗口外开销 | CPU 减墙钟、均值减 p50，不自洽；据它得出的"窗口外 25%"作废，订正为 **15%** |
| 7 | 无音轨/无视频轨时的 A/V 偏移 | 偏移值 | 无意义却照发数值（无视频轨时 `lastDiff` 从未被更新，实测 -846 → -1849 → -2852，每秒差 1000ms，就是个随时钟单调发散的数） |

**共同根因**：指标的**写入侧与读取侧隔了好几层**，写的时候看不见它最终会被当成什么来读。

**审代码防不住这一类**——采集点和数字本身都没错，错在读法。七次全部是真机跑出数据、觉得数字不对劲、回溯才发现的。

### 4.2 结论：新增指标必须同时写明"在哪个时刻采样"

`avOffMs` 是最干净的反例。它与 `syncWorstMs` **用同一个函数** `VEAVsync::getLastDiffUs()`，差别只在调用时机：

```
updateVideoPts(frame->getPts()) → 判丢帧 → getWaitTime → 睡眠 → 渲染
                ↑                                    ↑          ↑
          m_VideoPts 在这里写入            Timeline 多落在这里   syncWorstMs 在这里采样
```

`m_VideoPts` 在一帧处理的**开头**写入。Timeline 从 player looper 在任意时刻采样，绝大多数时候正落在那段睡眠里，取到的是"即将显示的帧的 pts − 当前时钟" = **剩余等待时间**，恒为正，且随流水线深度变化——两个素材上 72ms 与 24ms 的差异全部来自流水线深度，与同步质量毫无关系。而 `syncWorstMs` 在 `renderFrame` 之前采样，那时等待已结束，量的是真实余量。

**同一个函数，采样时刻的差别决定了一个纯误导、一个有意义。**

所以 `avOffMs` 直接删掉而不是改名保留：同步维度有 `syncWorstMs` 一个就够，它从一开始就是冗余的，冗余之外还误导。

### 4.3 自校验上线后立刻抓到第八次

`CpuGauge` 接上的**第一次运行**就报出：vdec 线程烧 43% CPU 而 `instrumented = 0.0`，覆盖率 0%。

成因是 CPU 侧**重复了这个项目早先在墙钟侧修过的同一个 bug**：`decodeBeginUs` 只覆盖 `receive_frame`，`send_packet` 的耗时由 `mDecodeAccumUs` 单独累加，CPU 起点没跟上那一段。当初墙钟侧的同一个错是 **0.1ms vs 实际 14ms，270 倍**（startup-cpu-opt 步骤2 修的就是它）。补上 `mDecodeAccumCpuUs` 后覆盖率 0% → **99%**。

**同一个错误，第七次靠人跑数据发现、第八次被机制当场逮住——这是本轮工作的核心价值。**

推论：**gap 大意味着该环节出问题时现有指标不会有任何反应。** 覆盖率本身就该是一个被持续监视的指标。

### 4.4 单位与统计量不得混用，但也不得为统一而牺牲信息

`gapMs` 第一版 `= 线程 CPU − (upload+draw+swap 的 p50 墙钟)`，同时混了两样：单位（CPU 时间 vs 墙钟）和统计量（本秒均值 vs p50）。当时把它描述为"下界"也是错的——两处偏差方向相反，它既不是下界也不是量值，只是一个不自洽的差。

改法：两边都用 CPU 时间、都用本秒均值。

但**三段仍保持墙钟不变**：`swap` 的墙钟正是等 vsync 的时长，换成 CPU 就把这个信息毁掉了。**两套量各有各的问题域，不能为了单位统一牺牲掉其中一个。**

### 4.5 交叉校验必须来自独立来源

本轮每个新增指标提交前都做了外部独立测量对账：

- CPU 列：shell 侧直接读 `/proc/<pid>/stat` 算得 91.0%，时间线自报 84.7~94.6%，一致；
- 渲染线程：外部读 `/proc/<tid>/stat` 得 4.80ms/帧，内部自报 4.79ms（外部按 tick 计，10ms 粒度有量化误差）。

## 5. 涉及模块

| 模块/文件 | 改动 |
|-----------|------|
| `utils/Log.h` | 新增 `ALOGF` 档 |
| `utils/VEPerfStats.h` | `Timeline`（VESTAT/VERENDER/VEGAUGE）、`CpuGauge`、`CpuScope`、`threadCpuUs()` |
| `core/VEPlayerDriver.cpp` | 22 处状态机留痕（ALOGW） |
| `core/VEPlayer.cpp/.h` | Timeline 驱动（挂 `kWhatProgressTick`）、同步余量与队列水位采样 |
| `core/VEVideoDecoder.cpp/.h`、`VEAudioDecoder.cpp/.h`、`VEDemux.cpp/.h` | CpuGauge / CpuScope 接入、每帧日志转 ALOGF |
| `core/VEVideoDisplay.cpp/.h` | 队列深度原子镜像、`noteSyncMargin` 收口 |
| `interface/VESource.h` | 新增 `getQueueDepth` |
| `platform/android/renders/VEGLESVideoRenderer.cpp` | 渲染三段 + `renderInstrumentedCpuUs` 自校验 |
| `platform/android/decoders/VEMediaCodecVideoDecoder.cpp` | 硬解侧 `noteSyncMargin` |
| `platform/android/renders/VEAudioSLESRender.cpp` | 每帧日志转档 |
| `NativeLib.java`、`native_PlayerInterface.cpp` | progress 三层重复日志合并 |
| `lzplayer_core/build.gradle`、`cpp/CMakeLists.txt` | `-PveTraceFrame=true` 开关 |
| `scripts/gen-test-assets.sh` | 新增 |

## 6. 风险与依赖

- **与 perf-metrics 的关系**：本 feature 交付的逐秒时间线正是 perf-metrics 设计稿 §8 指出的"结构性缺口"的填补，也是其**步骤7 跑分报告的前置条件**。时间线已就绪，跑分报告可以开始做。
- **与 decoder-test-redesign 的关系**：实现其 §5.2（逐秒时间线）与 §3（素材矩阵）。
- **素材可比性**：本轮 CPU 数字与历史记录的"软解 1080p 144.5%"**不可比**——那个数字来自已被删除的 1080p 基线素材。基线素材已由 `gen-test-assets.sh` 重建，后续数字以重建后的素材为准。
- **ALOGF 默认关闭的代价**：出问题时需要重新构建才能拿到每帧日志。权衡结论是可接受——配额丢弃 11 次意味着默认构建下**任何**日志都不可信，比"要重新构建"严重得多。
- **`VEGAUGE` 的 gap 不为零是常态**：adec 剩余 1.1%、demux 0.7% 是同一线程上其它消息处理与 looper 自身开销，不在 `onDecode`/`onRead` 之内，属预期，不必追平到 0。

## 7. 遗留

1. adec 剩余 gap 1.1%、demux 0.7%——同一线程上其它消息处理与 looper 自身开销，属预期，暂不追平。
2. 跑分报告（perf-metrics 步骤7）尚未产出，时间线已就绪，可以开始做。
3. 三个零覆盖能力维度（**网络源、长时稳定性、错误降级路径**）仍阻塞在故障注入手段上，本 feature 未解决。
