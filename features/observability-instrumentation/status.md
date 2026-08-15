# observability-instrumentation 日志埋点整改与可观测性建设 进度

> 最后更新: 2026-08-15
> 总体状态: Done（12/12 步骤全部完成并真机验证，10 笔提交已推送 origin/master_openclaw，`d3b5e81..204a88a`）

## Done
- [x] 步骤 1: 状态机留痕 —— VEPlayerDriver 4 处守卫拒绝 + 18 处状态迁移改打 ALOGW（用 W 是因为它是排查状态机问题的唯一线索，不能被 300 行配额挤掉或被 Release 剔除）(2026-08-15, commit f49237c)
- [x] 步骤 2: 每帧日志分档 —— Log.h 新增 ALOGF 档（默认关闭，`-PveTraceFrame=true` 打开），19 处每帧点位转入；判据是打印频率而非重要性。逃生开关实测恢复 647 条每帧日志 (2026-08-15, commit f49237c)
- [x] 步骤 3: 逐秒时间线基线 VESTAT —— 挂已有 kWhatProgressTick 不新增唤醒源，累计量一律发差值；队列水位改采瞬时深度（经 VESource::getQueueDepth）、轨道存在性改用轨道索引。实测日志 3533 → 150 行、配额丢弃 11 → 0 次 (2026-08-15, commit f49237c)
- [x] 步骤 4: 时间线补同步维度 —— 新增 syncMarginWorstUs（本秒最小余量，每秒 exchange 复位）；同步余量上报收口到 noteSyncMargin()，软硬解两处统一。实测正常片 -1.1 / 无音轨 -4.2 / 纯音频哨兵 (2026-08-15, commit f7ffe4d)
- [x] 步骤 5: 时间线补 CPU 列 —— native 自读 /proc/self/stat（不依赖 UI 线程喂数据），从最后一个 `)` 之后解析字段，首次采样返回哨兵。外部 shell 独立测得 91.0% vs 自报 84.7~94.6%，一致 (2026-08-15, commit c60e2da)
- [x] 步骤 6: 合并 progress 事件三层重复日志 —— 前两层转 ALOGF，第三层只跳过 progress 一种（它打的是所有事件，稀有事件仍有价值）。progress 相关 73 → 3 行、进程日志 150 → 116 行 (2026-08-15, commit 79ac9d8)
- [x] 步骤 7: 基线素材生成脚本 scripts/gen-test-assets.sh —— 对应 decoder-test-redesign §3；脚本入库而非素材入库；合成源用 testsrc2 叠 noise（纯色块会系统性低估负载）。ffprobe 逐个核对通过 (2026-08-15, commit 03672d4)
- [x] 步骤 8: 渲染开销分解上时间线 VERENDER + 分段之和自校验 —— 单独一行而非并进 VESTAT（硬解不经 GLES 渲染器）；renderThreadCpuUs 首次即测出每帧差 1.3~2.2ms 落在插桩窗口外。交叉校验内部 3.16~5.64ms/帧 vs 外部 4.67ms/帧 (2026-08-15, commit c93b038)
- [x] 步骤 9: 修正 gapMs 改用 CPU 减 CPU —— 上一版混了单位（CPU vs 墙钟）与统计量（均值 vs p50），"下界"的描述也是错的；据它得出的"窗口外 25%"作废，**订正为 15%**。三段仍保持墙钟（swap 墙钟正是等 vsync 时长，不为统一单位而毁掉该信息）(2026-08-15, commit bcdf252)
- [x] 步骤 10: 删除 avOffMs —— 它取自 getLastDiffUs 而 m_VideoPts 在一帧处理开头写入，采样多落在等待期，量的是"距下一帧上屏还剩多久"而非 A/V 偏移；与 syncWorstMs 同源仅差采样时刻，冗余之外还误导，故整列删除 (2026-08-15, commit 916654a)
- [x] 步骤 11: 分段自校验通用件 VEGAUGE —— 抽 VEPerfStats::CpuGauge，threadCpuUs() 提到 VEPerfStats.h；接 vdec/adec/demux。**首次运行即抓出 vdec 覆盖率 0%**（CPU 起点漏 send_packet 段，与项目早先在墙钟侧修过的 0.1ms vs 14ms 是同一个 bug），补 mDecodeAccumCpuUs 后升至 99% (2026-08-15, commit 5a80059)
- [x] 步骤 12: 补 demux/adec 分段插桩 —— demux 加 demuxReadUs 测 av_read_frame；adec 缺口在 receive_frame EAGAIN 等提前返回路径，改用 RAII CpuScope 覆盖所有返回路径。覆盖率 demux 0% → 68%、adec 40% → 69% (2026-08-15, commit 204a88a)

## Doing

## Todo
- [ ] 遗留 1: adec 剩余 gap 1.1%、demux 0.7% 追平（成因已定位：同一线程上其它消息处理与 looper 自身开销，不在 onDecode/onRead 内，属预期；优先级低，可不做）
- [ ] 遗留 2: 跑分报告产出 —— 归口 **perf-metrics 步骤7**，逐秒时间线已就绪，前置条件已满足，可以开工
- [ ] 遗留 3: 三个零覆盖能力维度（网络源 / 长时稳定性 / 错误降级路径）—— 仍阻塞在**故障注入手段**上，本 feature 未解决；与 decoder-starve-wake-dedup 步骤4、high-perf-player Phase 3 的网络源真机验证是同一批前置

## 经验记录（不计入三态，详见设计文档 §4）
- **本轮新增指标第一版错了七次，全部是"字段名承诺 A、实际度量 B"，且七次全部靠跑真机数据发现，没有一次是审代码发现的。** 共同根因：指标的写入侧与读取侧隔了好几层，写的时候看不见它最终会被当成什么来读。
- **结论：新增指标必须同时写明"在哪个时刻采样"。** avOffMs 与 syncWorstMs 用同一个函数，差别只在调用时机，而这个差别决定了一个纯误导、一个有意义。
- **自校验上线后立刻抓到第八次**：vdec 覆盖率 0%，成因是 CPU 侧重复了项目早先在墙钟侧修过的同一个 bug（漏掉 send_packet 段，当初墙钟侧是 0.1ms vs 实际 14ms）。同一个错误，第七次靠人跑数据、第八次被机制当场逮住 —— **这是本轮工作的核心价值**。
- **推论：gap 大意味着该环节出问题时现有指标不会有任何反应。** 覆盖率本身应作为持续监视的指标。
