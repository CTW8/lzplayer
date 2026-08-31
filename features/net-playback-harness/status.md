# 网络播放测试服务 进度

> 最后更新: 2026-09-01
> 总体状态: **Doing（步骤 1~10 全部 Done = 10/10；2026-09-01 原 Todo 两条均已了结，
> 余 1 条：`net-matrix-v2-2026-09-01` 矩阵结果待补记）**

## Done

- [x] **步骤10: no-range 顺序读回退** (2026-09-01, commit `7113228`) —— **验收 4 项全部完成，零回归。**
  - **修法与原登记的判断不同，已更正**：**不需要在上层补顺序读回退**。回退放在
    `VEHttpDataSource` 这一层即可 —— `IDataSource` 的契约是"按偏移取字节"，
    **用不用得上 Range 是实现细节**，不该泄漏给 demux 或缓冲层。
  - 实现要点：新增 `skipForwardTo()` 顺序读丢弃 + `mNoRange` **粘性标志**
    （只认 `200 且 offset>0` 这一处铁证；**`bytes=0-` 回 200 不构成证据**）；
    `readAt` 在 `mNoRange` 下向前跳走丢弃、**不重连**（重连拿到的还是从 0 的整个 body）。
  - **顺带修掉同形 bug**：200 响应下总长原按 `offset + Content-Length` 算，**凭空多一个 offset**，
    导致 EOF 判据永不成立、流尾被当成网络错误 —— 与已修的"EOF 报成 EIO"同形。
  - **素材换成 60s**：`run-net-matrix.sh` 的 no-range 素材由 11.3s 换成
    `probe-visual.mp4`（60s / 57MB / **moov 在文件尾**，最坏路径）。原素材 n=27<30，
    判据只能出 INCONCLUSIVE，**在汇总表里与"通过"无从区分**。
  - **验收 ①（稳态帧率）PASS**：p50=**29.9 fps**，n=55 ≥ 30；交叉素材 `r_frame_rate=30.00`。
    状态机 1→2→4→8→16 **全程未离开 STARTED**（原为 16→0 静默退回）。`dropOvf` max=0 PASS。
    RSS/fd 仍 INCONCLUSIVE（55s < 600s 门槛，**与其它场景同口径**，非本步骤缺陷）。
  - **验收 ②（seek 行为定案）PASS**：`70,20,85,40` 四次含向后跳，**4/4 成功**、
    `aborted` 全 false、精度 0.0/33.3/0.0/0.0 ms；另在 11.3s 素材上先验过一轮 3/3
    （精度 22.6/29.5/24.5 ms）。**代价已量化**：4 次 seek 在 57MB 素材上共传输 **91.1MB**
    —— 向后跳只能从 0 重来，**这是无 Range 服务端的固有代价，不是缺陷**。
  - **验收 ③（12 场景矩阵无回归）PASS**：`test-reports/net-matrix-2026-08-31/`，**零 FAIL**。
    与 08-22 基线逐条对照**只有两处变化**：`no-range` P1→P2（本次修复，INCONCLUSIVE 转 PASS）；
    `throttle-below` F1→F0 —— **不是修好了**，见 Todo 段新登记的判据缺陷。
    其余 10 场景 PASS/FAIL/INC 计数完全一致。
  - **验收 ④（提交）已完成** —— commit `7113228`「修复 no-range 静默退回 IDLE：HTTP 源补顺序读回退」。

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
      注：本机 ffmpeg 缺 libfreetype，drawtext 不可用，帧号烧录（A/V 同步外部地面真值）未做
      → **2026-08-28 已由 test-capability-v2 步骤6 用色带编码帧号绕开并交付**（`434381c`/`d688f9b`）。
- [x] 步骤8: 长稳 30 分钟 (2026-08-22) — 样本 1790 条 / 1790 个唯一秒号，稳态 1731 秒：
      fps 均值 23.9 最低 22.9；**RSS +3.6 MB/小时**（首 345.2 → 末 346.2，区间 341.4~356.7）；
      **fd −0.4 个/小时**（首末均 178）。**判定无内存泄漏、无 fd 泄漏。**
      **重要实证：短样本斜率不可靠** —— 5 分钟样本测得 RSS +41.6 MB/小时，30 分钟测得 +3.6，
      **相差 11 倍**。据此把 `gen-report.py` 泄漏判据门槛从 120 秒提到 600 秒。
- [x] 步骤9: 基线归档 (2026-08-22) — `test-reports/net-matrix-2026-08-22/`（12 场景 + 长稳
      + SUMMARY.txt）、`test-reports/net-baseline-2026-08-22/`。新增 `docs/test-capability-guide.md`。


## Doing

（空）

## Todo

- [ ] **`test-reports/net-matrix-v2-2026-09-01/` 结果待补记** —— 这是**第一轮所有注入都真正生效**
      的 12 场景矩阵（此前 stall 类注入因 `&` 被设备侧 shell 吃掉而从未真正下发，见下方
      2026-09-01 更正段）。跑完后须把逐条结论补进本文件与总表，
      并与 `net-matrix-2026-08-31/`、`net-matrix-final-2026-09-01/` 做跨轮次对照。

## 2026-09-01 已了结的 Todo

- [x] **throttle-below 的「稳态帧率达标」判据被误用** → **已修** (2026-09-01, commit `256f7d7` 第 4 项)
      —— 该场景的全部意义就是帧率应该塌下去，拿达标去判它 PASS 才是错的；
      08-22 那次 FAIL 一直靠人工覆盖，后来样本数掉到 `kMinSamples` 门槛下变成 INCONCLUSIVE
      **把问题藏了起来**。改为**按注入参数分场景**：限速在码率之下时期望反过来 ——
      **帧率必须下降，仍满帧说明注入根本没生效，比 FAIL 更该拦**。

- [x] **帧号烧录探针（A/V 同步的外部地面真值）对账完成** → **已交付**，
      见 **test-capability-v2 步骤6**（commit `434381c` + `d688f9b`）。
      步骤7 当时记的"本机 ffmpeg 缺 libfreetype、drawtext 不可用"已被绕开 ——
      改用**色带二进制编码帧号**（只判像素亮暗，比烧录数字更可靠）。本 feature 不再挂此项。

## 2026-09-01 更正：`stall-*` 两个用例此前从未真正注入过断流（commit `256f7d7`）

**根因**：URL 里的 `&` 被**设备侧 shell** 吃掉。脚本原有注释声称"整条命令加引号"已解决 ——
**没有解决**：引号在宿主机侧生效，而 `adb` 把 argv 拼成一个字符串交给**设备侧 shell 重新解析**，
`&` 在那里仍是裸的后台运行符。后果不只是丢注入参数，而是 **`&` 之后的所有 am 参数一起丢**
（`autoplay` / `caseName` / `playSeconds`），于是不起播、快照没名字、报告九条全 INCONCLUSIVE。
实测 `?kbps=2000&stall=6@0.25` 到服务端只剩 `kbps=2000`，状态机停在 8(PREPARED)。
修法：给 source 加单引号。

**因此**：步骤6 逐条表里 stall-recover 的"buffering 42/42 成对"与 stall-forever 的
"断流 300s 无 ANR"**不由这两个用例支持** —— 观察到的 buffering 全部来自限速注入。表已更正。

**同批修掉的另外三处判据缺陷**（共同形状：用例照常跑完、报告照常产出，**但什么都没验到**，
而这与"验过了、通过"在汇总表里长得一模一样）：
1. **时间线按 `playSeconds` 截断 → `post_seek` 段恒为空**：seek/变速/切轨三个序列全排在稳态段
   之后，拿 `playSeconds` 当上界会把整个序列期的样本丢掉 —— **历来每份报告的 `post_seek` 都
   不可能有数据，seek 之后的任何判断从来没做成过**。改为按序列结束截断（+3 秒观察窗）。
2. **`LOCAL_ASSET` 对设备绝对路径拼成 `assets/generated//sdcard/Movies/xxx.mp4`** → 文件找不到
   → `keyframes.txt` 静默为空 → **seek 精度判据无声地失去交叉校验来源**。改为先取 basename 并回退查找。
   修好后暴露出：**三个 `fault-hw-*` 用例指向的 `/sdcard/Movies/base-h264-1080p.mp4` 在设备上不存在**
   （`VEDemux::onPrepare couldn't open input`），此前每次都因指纹缺失被"拒绝出报告"（0/0/0）而无人察觉
   —— 已登记为 **test-capability-v2 Todo 第 2 条**。
3. throttle-below 判据误用（见上方已了结 Todo）。

**同时新增三条判据**（三条都做过"故意造错确认断言会失败"这一步）：
- **seek 精度落在帧栅格上**：校验 `accuracy ∈[0, 帧间隔)` **且**请求位置+accuracy 落在素材
  真实帧栅格上 —— **后者才是识破"把请求值原样回传"的那一条**；
- **seek 后队列峰值复位**：峰值只涨不落，序列出现下降即证明复位。**不判"post_seek 首样本为 0"**
  —— 峰值每秒采一次而队列一秒内就回填了，那样会把正确实现判成 FAIL；
- **buffering 事件成对**：把此前靠人工数的 START/END 配对自动化（允许差 1）。

**回归结果**：12 场景矩阵**真实场景零 FAIL**；`no-range` 与 `net-seek` 各 **7 条 PASS**
（新判据加入后判据数由 4 增到 7~9）。归档 `test-reports/net-matrix-2026-08-31/`、
`net-matrix-final-2026-09-01/`、`net-matrix-v2-2026-09-01/`（后者进行中）。

## 2026-08-31 对账

- 原 Todo 段里的「步骤5 / 步骤7 / 步骤8 未做」三条与 Done 段**自相矛盾**（三步 2026-08-22
  当天即完成），系当日多次改写残留，**已删除**。
- 原 Todo 段「步骤10 需用户先拍板（产品决策）」与本文件另一条"更正登记：不是产品决策"
  **同时存在**，两条并成一条，按更正后的定性执行；**总表 net-playback-harness 行里
  同样的旧登记也已同步更正**。
- **步骤10 的代码在登记之前就已写好并跑过一轮**（工作区未提交，见 Done 段步骤10），
  本次登记是**补记实况**而不是派活；剩余工作是验收口径而非实现。
  **2026-09-01 该步骤四项验收全部完成并标 Done；改动已提交（commit `7113228`）。**
- 本 feature 之后（08-23 ~ 08-28）产出的故障注入 / 状态机遍历 / 时序压力 / 生命周期矩阵 /
  格式矩阵 / A/V 地面真值 / 跨轮次对照**均未在 features/ 登记** ——
  **2026-09-01 经用户拍板合并登记为 `test-capability-v2`（七步全部 Done），此条已了。**

## 步骤6 逐条结果

| 场景 | 结论 | 实测 |
|---|---|---|
| baseline | PASS | long-53min 连续播放 28s，fps=23.9、`astarve`=0、队列稳定 |
| seek-http | PASS | 判据 **4/4**，`seekTrace.count=3`，网络源上 seek 正常工作 |
| throttle-below | PASS | 限速 0.6× 时 fps 掉到 1~3、buffering 37 次，播放器正确感知带宽不足 |
| slow-ttfb | PASS | 首字节延迟 2s 后仍正常播放，fps 23.9 |
| stall-recover | ~~PASS~~ → **2026-09-01 更正：该轮从未真正注入断流，原结论不由本用例支持**；修好后重测 PASS | 原记"断流两次各 6s、buffering 42/42 成对"—— **观察到的 buffering 全部来自限速注入**（`&` 之后的参数含 stall 被设备侧 shell 吃掉）。`256f7d7` 修好后重测（把限速抬到码率之上，让断流成为唯一变量）：**播放恢复了两次、buffering 186/186 精确成对** —— 结论与原登记一致，但**这是第一次由真实断流得出** |
| stall-forever | ~~PASS~~ → **2026-09-01 更正：同上，该轮从未真正注入断流** | 原记"断流 300s、40s 观察窗无 ANR 无崩溃"**不由本用例支持**；服务端日志第一次出现 `STALL begin 6.0s` 是在 `256f7d7` 之后 |
| bad-content | PASS | PNG 当媒体源，状态机 16 → **128(STATE_ERROR)**，**错误链路首次被真实错误穿过** |
| **no-range** | **FAIL（真问题）→ 2026-09-01 已修，PASS** | 原：16(STARTED) → **0(IDLE) 静默退回**，上层无从知道。现：60s 素材 p50=29.9fps、seek 4/4、全程 STARTED。见 Done 段步骤10 |

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
- "内存维度为零"这条结论**已随步骤5（2026-08-22 完成 RSS/fd 列）作废**，此处保留原文备查。
