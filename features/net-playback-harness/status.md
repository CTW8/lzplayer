# 网络播放测试服务 进度

> 最后更新: 2026-08-22
> 总体状态: **Done（9/9 步骤，2026-08-22）**

## Done

- [x] 步骤1: 素材普查落盘 + manifest (2026-08-22) — `scripts/scan-assets.sh` + `assets/manifest.json`，
      8 个素材复制到 serving 并给稳定别名；manifest 含 `cache_drain_sec`（cacheBytes ÷ 码率，供关键约束5 用）。
      素材与 manifest 全部 gitignore，**实测 `git status` 干净（3.8G 零泄漏）**。
      **顺带纠正立项时的 VFR 误判**：真 VFR 是 portrait-vfr-a/b 与 noaudio-tiny/odd；
      两个 `real-hevc-*` **不是 VFR**（`90000/3001` 只是时基写法，算出来 29.99 恒定）。
- [x] 步骤2: media-server.py (2026-08-22) — Range / 416 / 目录穿越防护 / 字节级请求日志，
      三种注入（限速 `kbps`、首字节延迟 `ttfb`、断流 `stall=秒@MB`）+ `norange` 强制 200。
      只绑 127.0.0.1，经 `adb reverse` 供给设备。
- [x] 步骤3: 接线 smoke (2026-08-22) — **网络路径首次执行，发现并修复五个 bug**（见备注）。
      "失败即是产出"这条设计判断被完全坐实。
- [x] 步骤4: harness 接入网络源 (2026-08-22) — `run-benchmark.sh` 以 `http://` 前缀判定网络源，
      服务器请求日志随产物归档。**修掉一处 harness 自身缺陷**：采样窗口在序列收尾后
      未随 app 退出关闭，之后的 VESTAT 行被算进窗口，曾误报"缺号 17 个"。
- [x] 步骤6: 场景矩阵（主体完成） (2026-08-22) — 8 个场景有明确结果，**7 PASS / 1 FAIL**；
      FAIL 项（no-range）已拆为步骤10 待用户拍板。逐条结果见下方"步骤6 逐条结果"。
      **注意：`throttle-above` 本轮未见结果，若未跑需补**（不计入上面的 8 条）。
- [x] 步骤5: VESTAT 加 RSS/fd 列 (2026-08-22) — 此前 `VEPerfStats.h` 里 56 处 "memory" 全是
      `memory_order` 误匹配，**内存维度完全为零**。rssMb 读 `/proc/self/statm`，fd 数
      `opendir /proc/self/fd`。**fd 与 RSS 是两种独立故障**，只看 RSS 会漏掉 fd 泄漏。
      读失败返回哨兵而非 0。外部交叉核对一致（内部 248.3~249.7 MB vs 外部 248.4 MB）。
- [x] 步骤6: 场景矩阵 12 个 (2026-08-22) — 全量跑通，**零 FAIL 误判**。唯一 FAIL 是
      throttle-below 的帧率判据（fps_p50 1.8），那正是限速 0.6x 下的**预期行为**。
      INCONCLUSIVE 均为口径正确工作（stall/no-range/bad-content 本就不该有稳态帧率；
      短素材 portrait-vfr 仅 9 秒样本不足 30）。固化为 `scripts/run-net-matrix.sh` 可一键复跑。
- [x] 步骤7: 画面正确性断言 (2026-08-22) — **项目第一条自动化画面判据**。四角红/绿/蓝/黄
      色块，实测 **4/4 通过**，色值 (255,24,0)/(0,215,0)/(0,15,255)/(255,240,0) 与期望
      高度吻合。一次覆盖四类此前完全测不到的故障：色彩空间（BT.601/709 × full/limited）、
      画幅拉伸、旋转元数据、上下颠倒。**视频区自动探测**而非写死比例——第一版按固定比例
      取四角全落在黑色控件区，误报 0/4。
      注：本机 ffmpeg 缺 libfreetype，drawtext 不可用，帧号烧录（A/V 同步外部地面真值）未做。
- [x] 步骤8: 长稳 30 分钟 (2026-08-22) — 样本 1790 条 / 1790 个唯一秒号，稳态 1731 秒：
      fps 均值 23.9 最低 22.9；**RSS +3.6 MB/小时**（首 345.2 → 末 346.2，区间 341.4~356.7）；
      **fd −0.4 个/小时**（首末均 178）。**判定无内存泄漏、无 fd 泄漏。**
      **重要实证：短样本斜率不可靠** —— 5 分钟样本测得 RSS +41.6 MB/小时，30 分钟测得 +3.6，
      **相差 11 倍**。据此把 `gen-report.py` 泄漏判据门槛从 120 秒提到 600 秒。
- [x] 步骤9: 基线归档 (2026-08-22) — `test-reports/net-matrix-2026-08-22/`（12 场景 + 长稳
      + SUMMARY.txt）、`test-reports/net-baseline-2026-08-22/`。新增 `docs/test-capability-guide.md`。


## Doing

（无）

## Todo

- [ ] **no-range 静默退回 IDLE**（state 16 → 0）—— **更正此前"待用户拍板"的登记：这不是产品
      决策，是设计意图与实现脱节**。`VEHttpDataSource` 已把 200 列为可接受状态码、注释明写
      "200 = 服务端忽略了 Range，从头给"、"上层只能从头读"，**设计意图就是回退顺序播放**。
      问题在实现：`statusCode == 200 && offset > 0` 直接 disconnect 返回 VE_INVALID_OPERATION，
      而上层没有"从 0 读并丢弃到目标偏移"的回退路径。mp4 解析必然要 offset>0（跳文件尾读 moov），
      第一次跳转就撞上。修法改动面较大，未实施；当前已在代码注释与日志里标明会导致静默失败。
- [ ] 帧号烧录探针（A/V 同步的外部地面真值）—— 阻塞于本机 ffmpeg 缺 libfreetype。

- [ ] 步骤5: VESTAT 加 RSS/fd 列（native 小改）—— **未做，内存维度仍为零**；步骤8 的前置
- [ ] 步骤7: 探针素材 + 像素断言（含"故意造错确认断言会失败"这一步）—— 未做
- [ ] 步骤8: 长稳（53min 素材 × 限速 1.2× × 循环，RSS/fd 斜率）—— 未做，**依赖步骤5**
- [ ] 步骤10: **no-range 行为定案与实施 —— 需用户先拍板**（产品决策，非实现选择）：
      服务端不支持 Range 时播放器 16(STARTED) → 0(IDLE) **静默退回**，既不播也不报错。
      候选 (a) 回退 200 全量顺序播放、seek 明确失败；(b) 明确进 STATE_ERROR。
      详见设计文档 §9.3。

## 步骤6 逐条结果

| 场景 | 结论 | 实测 |
|---|---|---|
| baseline | PASS | long-53min 连续播放 28s，fps=23.9、`astarve`=0、队列稳定 |
| seek-http | PASS | 判据 **4/4**，`seekTrace.count=3`，网络源上 seek 正常工作 |
| throttle-below | PASS | 限速 0.6× 时 fps 掉到 1~3、buffering 37 次，播放器正确感知带宽不足 |
| slow-ttfb | PASS | 首字节延迟 2s 后仍正常播放，fps 23.9 |
| stall-recover | PASS | 断流两次各 6s，**buffering 42 次 / 恢复 42 次完全成对**，不崩溃、断流后继续播放 |
| stall-forever | PASS | 断流 300s，40s 观察窗内无 ANR 无崩溃 |
| bad-content | PASS | PNG 当媒体源，状态机 16 → **128(STATE_ERROR)**，**错误链路首次被真实错误穿过** |
| **no-range** | **FAIL（真问题，未修）** | 16(STARTED) → **0(IDLE) 静默退回**，上层无从知道。→ 步骤10 |

另：素材矩阵四条真实轴全通过 —— portrait-vfr-a（竖屏 VFR）30.9fps /
noaudio-odd（无音轨）27.9fps 且 `aq=-1` 哨兵语义正确 / real-hevc-4k 29.9fps /
long-2h-dualaudio（双音轨）23.9fps。
net-baseline 报告 3/4：未发起 seek 时判 INCONCLUSIVE，是判据**正确地拒绝空洞的绿**，不是缺陷。

## 备注

### 步骤3 修复的五个 bug（全部已验证）

**同一类：本地文件永不触发的分支，在网络源上是常态。**

1. `VEBufferedDataSource::readAt` 左沿每读必进 → mp4 回读触发 reposition **死循环**
   （原注释"demux 不会回头读"**是错的**）
2. buffering 事件**乱序**：状态变更与投递不在同一临界区，多线程 `readAt` 下 END 抢在 START 前
3. `readAt` 等待循环在左沿越过自己时**永久卡死**
4. `VEHttpDataSource` 在文件尾把 **EOF 报成 EIO**（EOF 判定排在重连之后，永远执行不到）
5. buffering **恢复信号来自被自己暂停的路径**：START 暂停数据面 → demux 停止调 `readAt`
   → END 只能由 `readAt` 发出 → **永远不恢复**

### 指标语义更正（重要）

`vstarve`/`astarve` 在 throttle-below 下**仍为 0**，而队列有数据。
它度量的是"**解码器等不到包**"，限速下瓶颈在更上游 —— 数据被 buffering 挡在数据面之外，
解码器侧队列反而是满的。
**这个计数器不是"网络供给不足"的指标；要看网络供给应看 buffering 次数与时长。**
smoke 阶段观察到的 `astarve` 12~15/秒 是 **bug 状态下的产物**（数据取不出来，解码器真饿着），
**那组数字不能作为任何基线**；修好后正常限速路径下它不该增长。

### decoder-starve-wake-dedup 步骤4：**未解锁**（立项承诺被推翻）

plan 与设计文档 §6 曾把"本 feature 解锁该步骤"列为明确交付物，依据是
"throttle-below 提供能持续饥饿的源"。按上一条实测，**该前提不成立** ——
限速下解码器根本不饥饿。在拿到 `vstarve > 0` 的实测之前，不得声称已解锁。
可能方向（未验证）：绕开 buffering 暂停机制后限速，或压小 `VEBufferedDataSource` 水位。

### 沿用的既有结论

- 设计文档 §1.1 的前提更正（自定义 `AVIOContext` + 自写 `VEHttpDataSource`，
  FFmpeg 只做 demux）已由本轮真机执行**间接坐实** —— 五个 bug 全在自写代码里。
- "内存维度为零"仍然成立（步骤5 未做）。
