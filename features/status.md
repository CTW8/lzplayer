# 需求总进度

> 最后更新: 2026-08-09

| Feature | 状态 | 完成度 | 当前工作 | 目录 |
|---------|------|--------|----------|------|
| data-model-fixes | Doing | 6/7 步骤 | 步骤7 真机回归（等待设备） | [data-model-fixes/](data-model-fixes/) |
| lzplayer-test-expert | Doing | 2/3 步骤 | 等待 agent 注册确认 | [lzplayer-test-expert/](lzplayer-test-expert/) |
| deep-review-fixes | Done | 13/13 步骤 | -（全部提交完毕；遗留：真机回归按用户指示挂起） | [deep-review-fixes/](deep-review-fixes/) |
| protocol-hardening | Done | 9/9 步骤 | -（全部提交完毕；遗留：真机回归挂起） | [protocol-hardening/](protocol-hardening/) |
| demux-buffering | Done | 3/3 步骤 | -（三 commit acbcc93/9c8f191/097887e；遗留：真机回归挂起） | [demux-buffering/](demux-buffering/) |
| hygiene-debt | Done | 4/4 必做步骤 | -（4 commit 9740456/11f9889/6115acd/382a45e；步骤5 可选项经用户否决不做；遗留：真机回归挂起） | [hygiene-debt/](hygiene-debt/) |
| demux-nuplayer-refactor | Doing | 4/5 步骤 | 步骤5 真机回归（prepare/start/pause/seek/EOS/换源 reset） | [demux-nuplayer-refactor/](demux-nuplayer-refactor/) |
| high-perf-player | Doing | 6/6 阶段代码完成（待真机回归） | 全阶段真机回归 + 提交。**Phase 4 已记录一处缺陷（饥饿双唤醒源未去重），修复独立跟踪于 decoder-starve-wake-dedup**；其步骤4 与 Phase 3 网络源真机验证建议合并做 | [high-perf-player/](high-perf-player/) |
| test-console-ui | Doing | 7/8 步骤代码完成（待真机验证） | 步骤8 真机自测 —— **C 屏须按 perf-metrics 步骤5 改后的六分页性能面板核对，不按本 feature 原八项读数单页**；**2026-08-09 起稳态页还多了「丢帧构成 · 按原因分类」区块，且全部读数改为三态（`null`→`--`、负数标红原样显示），核对时按此现状** | [test-console-ui/](test-console-ui/) |
| vulkan-renderer | Doing | 2/7 步骤 | 步骤7 真机验证**阻塞**：测试机锁屏无法解锁（surface 被回收致零渲染）；步骤2~5 代码已编译通过、Vulkan 初始化真机实测成功，但 renderFrame 未跑过一帧 | [vulkan-renderer/](vulkan-renderer/) |
| no-audio-audioonly | Done | 4/4 步骤 | -（同日实施并真机验证通过；遗留：MediaSelector 补 AUDIO 类型、起播 0.6s 追平） | [no-audio-audioonly/](no-audio-audioonly/) |
| startup-cpu-opt | Doing | 3/4 步骤 | **步骤4（2026-08-08 第二次重排）**：现行定序 **4a 硬解 codec configure 拆分与预热 → 4b 软解首帧不等同步时钟（新增）→ 4c 音频路径 CPU 复查（位置未变）**。<br>**4a 当前最大项**：configure 61~66ms = 硬解启播 169ms 的 39%。**第一个交付物是拆分数据而非优化代码** —— 在 `VEStartupTrace` 增 `T4A_CODEC_CREATED`/`T4A_CODEC_CONFIGURED`，把 T4a 拆成 创建/配置/启动 三段。<br>**4a 拆分已出数（工作区，未提交）**：configure 总 64~87ms 中 `createDecoderByType` 独占 **50~66ms（76~78%）**、`AMediaCodec_configure` 仅 **5.6~7.7ms** → 命中"创建占大头"分支走**预热**；**"configure 与 `find_stream_info` 并行"那条大改动被数据否掉**（只值 5.6~7.7ms）。已实施 `VECodecWarmup`（`open_input` 返回后按已知 codec_id 后台预建，藏进 find_stream_info 的 51~66ms 窗口；解码器 `take(mime)` 取不到照常自建；`onRelease` 调 `discard()` 兜底）。**待真机验证硬解启播总耗时下降 + 预热未取用不泄漏；改动未提交。**<br>**4b 新增**：软解「首帧上屏 57ms」主体是 AVSync 等音频时钟起锚，靠"首帧不等时钟、解出即画，之后再对齐"消掉，改动比零拷贝小；风险是首帧略早于音频，需确认无可见跳动。<br>**~~4a-旧 零拷贝上屏~~ → 实测撤回不做**：`present` p50 4.4ms 拆为 上传 1.8 / 绘制 0.3 / eglSwapBuffers 1.8，零拷贝只省上传的 1.8ms = 单核 5.4% = 软解总 CPU 144.5% 的 3.7%；代价是 `AHardwareBuffer_lockPlanes` 要 API 29（minSdk 24）、丢掉已修好的 BT.601/709×full/limited 控制、要 FFmpeg 自定义 get_buffer2 + 缓冲池 + EGLImage 生命周期且仍需回退路径。**顺带更正归因：`video_render` 27% 里只有约 13% 在 `renderFrame` 内，另一半在 AVSync 调度/消息循环/帧队列，把 27% 全归给三平面上传不准。**<br>**纹理预分配 → 已实施提交但收益记 0**：首帧上屏三次实测 51.7/98.2/58.8ms 与改前同量级、方差大；因 T6→T7 含 AVSync 等待，铁证是同批数据 `present` max 仅 9.98ms。代码无害保留。<br>**已记为本 feature 第三次同类错误**（硬解解码耗时 / 软解解码耗时 / 首帧上屏，三次都靠先做拆分测量才拦住）；**新增规则：动手优化某个读数之前先确认它的构成，不能只看它的名字**。<br>动手前仍须确认 vulkan-renderer 工作区状态（4a 已不碰渲染文件，但 **4b 会碰 `VEVideoDisplay`/`VEAVsync`**）。<br>**步骤1~3 已于 2026-08-08 实施并真机验证 Done**：① 探测调优（`ProbeLimits` 结构体 + 虚方法，本地 512KB/1s/fpsProbeFrames=0，**网络源沿用 FFmpeg 默认**）→ 解析流信息 硬解 145.0→**66.1ms**、软解 133.8→**51.1ms**，启播总耗时 231.2→**169.0** / 246.4→**209.1ms**；**功能验证全过**（时长 9529ms 与 ffprobe 一致、codec 正确、渲染帧数 278/287 与调优前完全一致、30.1fps 间接印证帧率未丢）。② 解码打点修正（`mDecodeAccumUs` 累加 send_packet，`onFlush` 清零）→ 视频 p50 **0.1→14.0ms**、音频 **0.05→0.3ms**，与 `vdec_thread` 79.5%÷30fps≈26.5ms 交叉校验同量级。③ **ALOGV 对照实验：假设被证伪** —— 差值仅软解 −1.5pp / 硬解 −4.5pp，**日志不是 CPU 瓶颈**，收敛降级为"可选清理"（理由只剩 logcat 300 行配额）；开关 `VE_QUIET_LOG` 默认 OFF 保留作对照工具。<br>**新观察（4c 由来）**：音频路径在软/硬解两条路都占 **21~26%** 单核 CPU，对 AAC 立体声 44.1k 偏高且 1.0x 不该有变速开销。 | [startup-cpu-opt/](startup-cpu-opt/) |
| decoder-starve-wake-dedup | Doing | 3/4 步骤 | **步骤4 正向验证「两个唤醒源互斥」生效** —— 代码已改完（两个解码器各加 `mStarveGen`，饥饿的通知与 500ms 兜底两条唤醒消息带同一代次，先到者胜）、**无回归已真机验证**（1080p 硬解/软解/纯音频均到 COMPLETED，渲染帧数 278/287/0 与改前逐项一致、丢帧 0、时长 9529/9529/10000ms）。**但修复生效本身没验到**：本地文件几乎不饥饿（1080p 实测饥饿次数 = 0，守卫未被执行；纯音频饥饿 1 次但作废计数 0，推断饥饿在流尾、兜底到达时先被 `!mIsStarted` 拦下）。前置需加可读回的 `starveCount`/`staleStarveWakeCount`（写文件读回或挂 perf-metrics 面板，**不得只依赖 logcat**）—— **2026-08-09 已在 perf-metrics 登记为步骤9d（第一梯队第 4 项「饥饿次数与每次持续时长」），两者互为前置，应合并做一轮真机验证**；路径选 **(a) `adb reverse` + 限速 HTTP 源**，顺带覆盖 high-perf-player Phase 3 网络源零真机验证的空白。缺陷来源是 **high-perf-player Phase 4**，非 demux-nuplayer-refactor（详见设计文档「归属判断」）。步骤1~3 改动未提交。 | [decoder-starve-wake-dedup/](decoder-starve-wake-dedup/) |
| perf-metrics | Doing | 3/13 步骤（步骤1/2/9a Done；步骤5 主体完成剩 3 项交付；新增步骤9 第一梯队 5 子项） | **2026-08-09 新增步骤9「指标体系补齐 · 第一梯队」（用户已批准从第一梯队开始做），子项顺序即实施顺序**：9a 丢帧原因分类 **已完成并真机验证** → 9b 音频 underrun（**本次开工项**）→ 9c credit park 次数 → 9d 饥饿次数与时长 → 9e 上屏间隔抖动。<br>**9a 是度量缺口修复不是新增指标**：`++mDroppedFrames` 全工程只有两处、都是"同步判定太晚"，另三条路径完全不入账（队列溢出兜底 `kMaxFramesBackstop`、代次过期 `queueGen` 不匹配、精准 seek 追帧）—— **"丢帧 0"的真实含义是"没有因迟到而丢帧"，不是"没丢帧"**，与本轮那四次"名字与实际度量不符"同源。`VEPerfStats` 加四个原子计数（`dropLate`/`dropOverflow`/`dropStale`/`dropSeekCatchup`）、采集点五处、**溢出路径此前连 `mDroppedFrames` 都不加也已补上**。**真机立刻证实**：1080p 软解 seek 一次 → 旧口径 `droppedFrames=0` 而 **`dropSeekCatchup=6`**。分类语义（late=问题 / overflow=出现即 bug / stale=正常 / seekCatchup=非缺陷）已入设计稿 C4 与 5.3。<br>**UI 完善两处（2026-08-09）**：① 稳态页「丢帧构成 · 按原因分类」四行带语义说明、按类配色，已截图核对；② **补上三态显示（前一步欠账）** —— 面板原用 `optDouble(key, -1.0)` 会把 native 的 JSON `null` 悄悄变回 −1，**等于在 UI 层重新制造 native 刚消除的状态混淆**，已改 `msOrNull()`：`null`→`--`、负数原样显示并标红注"← 负值：采集点顺序颠倒"。已写成设计稿 3.C + plan 关键约束 10 的硬性规定。<br>**新增设计稿 §8「指标体系评审结论」**：三梯队（二：卡顿次数与停顿时长 / 消息投递延迟 / 网络源指标；三：内存分类、温度降频）+ 按环节缺口（demux / 解码 / 渲染 / 音频）+ **两条准入门槛**（必须能改变某个决策；必须有独立来源可交叉校验）+ **一个结构性缺口：现有指标全是聚合快照没有时间线**，p95 正常但某一秒全塌了聚合值看不见 → 步骤7 报告必须出**逐秒时间线**。<br>**Doing 已堆 3 个（步骤3 / 步骤5 / 9b），超出 1~2 的约定** —— 建议先做掉步骤5 剩余项 1、2（全屏拖动 + 清每 tick 写文件脚手架，不依赖任何人）让它退出 Doing；步骤3 与 9d 都需"能触发 seek / 能造饥饿"的手段，合并成一轮真机工作。<br>**9a 与 UI 两处改动均未提交。**<br>—— 以下为 2026-08-08 原状态 ——<br>**两个 Doing**：① 步骤3 seek 追踪 —— `utils/VESeekTrace.h` 容器已就绪（环形 10 条/三阶段/精度/abort 入库），**待接 VEPlayer 三阶段打点与 JNI-Java getter**，头文件尚未被任何 .cpp 引用；需先造出触发 seek 的手段，一并验掉步骤2 遗留的「seek 后队列峰值归零」。② 步骤5 性能面板 —— 六分页（概览/启播/Seek/稳态/资源/日志）已真机截图逐页核对通过，**剩余**：面板不支持拖到全屏高度、每 tick 写 perf 文件的脚手架待清、Seek 页等步骤3 接通。<br>**2026-08-08 计划外顺序调整**：因用户反馈"做完两步界面看不到变化"，提前实施步骤5 并跳过步骤4；**步骤4 已重定位为「后续重构」（抽 StartupTrace/SeekTrace/PlayerStats 扩展类），不再是步骤5 前置**，触发时机为步骤7 成为第二个消费方时。<br>另：设计稿 3.A.3「不另开定时器」适用范围已收窄为**仅 HUD**，面板自有 1s ticker。<br>**2026-08-08 基线作废 + 一处口径缺陷**：本 feature 步骤1/2 的全部实测数字用的是 640×360 合成小素材，**只能证明采集点完整，不可作为性能基线**；"硬解慢 8.6 倍、瓶颈是 configure"的结论已被 1080p 素材推翻。软解 `videoDecodeMs` 打点漏掉了 `avcodec_send_packet`（**量错了对象**），~~在 startup-cpu-opt 步骤2 修~~ → **已于 2026-08-08 修完（startup-cpu-opt 步骤2）：1080p 软解正确值 视频 p50=14.0/p95=23.6、音频 p50=0.3/p95=0.65；本 feature 记录的 0.1ms / 0.05ms 两组旧值作废且与新口径不可比**。另撤回「缓冲按包数封顶」的怀疑（1080p 峰值仅 64~71 < 640×360 的 216，证明上限本就按字节/时长算），RSS 254MB 降级为"待测"。<br>**另：本 feature 提的「日志分级收敛」建议中"打日志消耗 CPU、与测量目标冲突"这条理由已被实测证伪**（startup-cpu-opt 步骤3：软解 −1.5pp / 硬解 −4.5pp），仅剩"logcat 300 行配额损害可调试性"这条理由；对关键约束 1 的影响是 **ALOGV 对 CPU 测量的干扰实测在 5pp 以内，不构成测量有效性问题**。<br>**2026-08-08 新增下游依赖**：startup-cpu-opt 步骤4a 将在 `VEStartupTrace` 增 `T4A_CODEC_CREATED`/`T4A_CODEC_CONFIGURED`，把 T4a 拆成 创建/配置/启动 三段 —— 本 feature 的采集点清单与面板启播页届时需同步扩充。 | [perf-metrics/](perf-metrics/) |

## 环境备注（2026-08-08 起，影响所有 feature 的真机验收）

- 测试设备已更换：小米 `fa04e593` → **OPPO PHK110 `e9d6e706` / Android 16 / ColorOS**。
- 该机**单进程 logcat 有 300 行配额**，超出静默丢弃（`W LOG_FLOWCTRL: ...OVER PROC QUOTA(300)...DROPPED`）。
  本工程 native 的 ALOGV 每秒即打满配额，会把应用自身的 Log.i/Log.d 一起丢掉。
  **验证任何东西都不得只依赖 logcat**：被测代码写文件，再
  `adb shell run-as com.example.lzplayer cat files/<path>` 读回。
- `pm grant` 在该机被 SecurityException 拦截，权限需手动授予。
- ~~待决策：native ALOGV 密度过高~~ → 已登记为 startup-cpu-opt 步骤3，
  **2026-08-08 对照实验完成：ALOGV 不是 CPU 问题**（关掉只省 软解 1.5pp / 硬解 4.5pp），
  「日志分级收敛」**降级为可选清理**，唯一理由就是上面这条 300 行配额损害可调试性。
  已有静音构建开关 `VE_QUIET_LOG`（默认 OFF，加 `-PveQuietLog=true` 打开），
  需要干净日志或干净 CPU 对照时可用。

## 测量素材规范（2026-08-08 起，影响所有 feature 的性能类验收）

- **性能基线必须用有代表性的素材**：标准素材为设备上的
  `/sdcard/Movies/VID_20230814_205835.mp4`（1920×1080 h264 30fps 20Mbps 9.5s + AAC）。
- **ffmpeg 造的 640×360 合成小素材只能用于验证采集点完整性与字段自洽，不可作为性能基线。**
  它会**系统性单向掩盖**与数据量成正比的瓶颈（容器探测/解码/纹理上传/内存）：
  perf-metrics 在它上面得出的瓶颈排序被 1080p 素材整体推翻
  （解析流信息 2~4ms → 133~145ms，从可忽略项跃升为启播第一大项）。
- **换素材必须重测全部结论**，不能只重测怀疑的那一项 —— 变的可能不是一个数字，而是排序。
- 每次测量都要记录素材指纹（分辨率/编码/帧率/码率/时长/有无音轨）；
  跨素材的数字比较必须显式标注不可比。
- **每个新指标提交前必须有一个独立来源的交叉校验**（例：分位数 × 帧率 ≈ 线程 CPU）。
  只有单一来源的数字不得进面板 —— `videoDecodeMs` 两次量错对象都是因为缺这一步。
- **（2026-08-08 追加）动手优化某个读数之前，先确认它的构成，不能只看它的名字。**
  一个里程碑区间/分位数往往由多段组成，可优化的那段可能只占一小部分，甚至根本不在这个区间里。
  startup-cpu-opt 已在此连续栽三次（硬解解码耗时 → 实为背压等待；软解解码耗时 → 漏 send_packet；
  软解首帧上屏 57ms → 实为 AVSync 等音频时钟起锚），**三次都靠先做拆分测量才拦住**。
  上一条"交叉校验"规则**防不住这一类**：采集点与数字都没错，错在读法。
  任何以"某段耗时占大头"为由的优化，提案里必须先给出该段的构成拆分。
- **（2026-08-09 追加）一个计数器不得覆盖多条语义不同的路径 —— 先数清路径，再看数字。**
  perf-metrics 步骤9a 暴露的第五次同类问题：「丢帧」有四条路径，
  但 `++mDroppedFrames` 只加在其中一条（同步判定太晚）上，另三条（队列溢出兜底、
  代次过期、精准 seek 追帧）**完全不入账**，于是"丢帧 0"的真实含义是
  "没有因迟到而丢帧"。**零值最危险**：它同时可以表示"没发生"和"没在数"。
  规则：新增或阅读任何计数类指标前，先在代码里穷举所有会触发它的路径，
  确认每条都有采集点，且**语义不同的路径分成不同字段**（各自带"这算不算缺陷"的说明）。
- **（2026-08-09 追加）UI 层不得用默认值把 native 的 `null` 兜掉。**
  `optDouble(key, -1.0)` 会让"没采到"与"采到了但是负数（采集点顺序颠倒）"
  在界面上变成同一个读数，**等于在展示层重新制造采集层刚消除的状态混淆**。
  一律三态：`null` → `--`，负数原样显示并标红，正常值正常显示。
