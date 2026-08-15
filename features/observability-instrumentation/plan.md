# observability-instrumentation 日志埋点整改与可观测性建设 实施计划

> 设计文档: ../../docs/observability-instrumentation-design.md
> 创建日期: 2026-08-15（补登记，实施已于同日全部完成）
> 提交范围: `d3b5e81..204a88a`（10 笔，已推送 origin/master_openclaw）

## 方案摘要
起因是 review 发现日志"密度与价值倒挂"：12 秒播放打 3533 行、触发 ColorOS 配额 11 次导致有用日志被静默丢弃，
而状态机 22 处迁移几乎不打日志。方案四部分：状态机留痕（ALOGW）、每帧日志分档（新增 ALOGF，默认关闭）、
逐秒时间线（VESTAT/VERENDER/VEGAUGE 三行，挂已有 progress tick 不新增唤醒源）、分段自校验（CpuGauge/CpuScope，
对账"分段之和 vs 独立测得的线程总量"）。配套 `scripts/gen-test-assets.sh` 重建基线素材矩阵。
逐秒时间线是 `docs/decoder-test-redesign.md` §5.2 的要求，也是 perf-metrics 步骤7 跑分报告的前置条件。

## 关键约束
1. **新增指标必须同时写明"在哪个时刻采样"**。本轮七次错误全是"字段名承诺 A、实际度量 B"，
   `avOffMs` 与 `syncWorstMs` 用同一个函数、只差调用时机，而这个差别决定了一个纯误导、一个有意义。
2. **累计量一律发差值**，不发绝对值——从绝对值求差要求采样无丢失，而日志恰恰会被配额丢。
3. **"没测到"用哨兵 `-9999`（队列深度用 `-1`），绝不用 0**。0 对余量/偏移/CPU 都是有意义的读数。
4. **不新增周期唤醒源**，时间线一律挂 `kWhatProgressTick`。
5. **每个新增指标提交前必须有独立来源的交叉校验**（内部自报 vs shell 侧读 `/proc/.../stat`）。
6. **分段记账用 RAII（`CpuScope`）而非手工在各出口记账**——漏一条提前返回路径就静默失真。
7. **每帧日志转档的判据是打印频率，不是重要性**：一次 seek/EOS/回退只打一条的保留原级别。
8. **单位与统计量不得混用；但也不得为统一而牺牲信息**——渲染三段保持墙钟，因为 `swap` 的墙钟正是等 vsync 的时长。

## 实施步骤
1. **状态机留痕**：`VEPlayerDriver` 4 处守卫拒绝 + 18 处状态迁移改打 ALOGW。
   验收：`stop→play` 静默失败可从日志追溯；级别为 W，Release 构建保留、不被 300 行配额挤掉。
2. **每帧日志分档**：`utils/Log.h` 新增 `ALOGF` 档（默认关闭，`-PveTraceFrame=true` 打开，
   build.gradle + CMakeLists 传递），19 处每帧点位转入。
   验收：默认构建日志量大幅下降；开关打开可完整恢复每帧日志。
3. **逐秒时间线基线（VESTAT）**：`VEPerfStats::Timeline` 挂 `kWhatProgressTick`，
   发帧率、丢帧四类分解、队列水位 aq/vq/fq（经 `VESource::getQueueDepth` 采**瞬时深度**，
   轨道存在性用**轨道索引**而非队列指针非空）。
   验收：12s 软解日志 3533 → 150 行、配额丢弃 11 → 0 次、播放进度与改前一致；
   水位语义三素材验证（纯音频 vq/fq=-1、无音轨 aq=-1、正常片均有值）。
4. **时间线补同步维度**：新增 `syncMarginWorstUs`（本秒最小余量，每秒 exchange 取走复位，
   取最小而非均值——一秒里一帧险些迟到是丢帧前兆，均值看不出来）；
   同步余量上报收口到 `noteSyncMargin()`（软解 VEVideoDisplay / 硬解 VEMediaCodecVideoDecoder 两处）；
   A/V 偏移要求两条轨都在才发，否则哨兵。
   验收：正常片 syncWorstMs -1.1 / 无音轨 -4.2 / 纯音频哨兵。
5. **时间线补 CPU 列**：native 自读 `/proc/self/stat` 的 utime+stime（不依赖 Kotlin 侧喂数据，
   跑分场景无人看 UI，依赖 UI 线程则 UI 一卡这列就断）；解析从最后一个 `)` 之后数字段
   （comm 允许含空格和括号）；首次采样返回哨兵。
   验收：外部 shell 读 `/proc/<pid>/stat` 算得 91.0%，时间线自报 84.7~94.6%，一致。
6. **合并 progress 事件三层重复日志**：前两层（VEPlayerDriver / native_PlayerInterface）转 ALOGF；
   第三层 `NativeLib.postEventFromNative` **只跳过 progress 一种**，保留其它所有事件。
   验收：progress 相关 73 → 3 行、进程日志 150 → 116 行、配额丢弃 0，保留下的正是 what=258/259/263。
7. **基线素材生成脚本**：`scripts/gen-test-assets.sh`，对应 `docs/decoder-test-redesign.md` §3；
   合成源用 testsrc2 叠 noise。
   验收：ffprobe 逐个核对四个基线的分辨率/帧率/时长；noaudio 确无音轨、shortaudio 为视频 10s + 音频 3s。
8. **渲染开销分解上时间线（VERENDER）+ 渲染自校验**：upload/draw/swap 三段逐秒化，
   单独一行不并进 VESTAT（硬解走 releaseOutputBuffer 不经 GLES，否则那半行永远是哨兵）；
   加 `renderThreadCpuUs`，每秒与三段之和比对。
   验收：交叉校验内部自报 3.16~5.64ms/帧 vs 外部 `/proc/<tid>/stat` 4.67ms/帧。
9. **修正 gapMs（CPU 减 CPU）**：新增 `renderInstrumentedCpuUs`（区间首尾各取 `CLOCK_THREAD_CPUTIME_ID`），
   `gapMs = 线程总 CPU − 区间 CPU`，两边同单位同统计量；三段仍保持墙钟。
   验收：threadCpuMs 5.63 / inCpuMs 4.79 / gapMs 0.84，窗口外由 25% **订正为 15%**；外部读得 4.80ms/帧。
10. **删除 avOffMs**：确认它取自 `getLastDiffUs()` 而 `m_VideoPts` 在一帧处理开头写入，
    采样多落在等待期，量的是"距下一帧上屏还剩多久"而非 A/V 偏移；整列删除而非改名保留。
    验收：同步维度仅剩 `syncWorstMs`，其语义经 `renderFrame` 前采样确认为真实余量。
11. **分段自校验通用件（VEGAUGE）**：抽 `VEPerfStats::CpuGauge`（threadUs / instrumentedUs / 每秒差值相减），
    `threadCpuUs()` 从渲染器提到 VEPerfStats.h；接 vdec / adec / demux 三处；
    demux 无分段计时时如实报 gap=100% 而非假装覆盖。
    验收：机制首次运行即抓出 vdec 覆盖率 0%（CPU 起点漏 send_packet 段），补 `mDecodeAccumCpuUs` 后升至 99%。
12. **补 demux/adec 分段插桩**：demux 加 `demuxReadUs` 直方图测 `av_read_frame`；
    adec 缺口在 `receive_frame` EAGAIN 等**提前返回路径**，改用 RAII `CpuScope` 覆盖所有返回路径，
    删掉手工 note。
    验收：覆盖率 demux 0% → 68%、adec 40% → 69%；剩余 gap（adec 1.1% / demux 0.7%）
    确认为同线程其它消息处理与 looper 开销，属预期。
