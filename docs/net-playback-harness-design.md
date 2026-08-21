# 网络播放测试服务（net-playback-harness）

> 创建日期: 2026-08-21
> 关联: docs/decoder-test-redesign.md（§2 故障注入 / §4 双来源判据 / §5 自动化）、
> docs/high-perf-player-design.md（Phase 3 网络源）、
> docs/decoder-starve-wake-dedup-design.md（步骤4 阻塞在"缺一个能持续饥饿的源"）、
> features/perf-metrics（步骤7 harness 与报告格式，本 feature 直接复用）

## 0. 一句话

在宿主机上起一个**只绑回环、可注入故障、带字节级请求日志**的 HTTP 视频服务，
经 `adb reverse` 供给设备，把播放器的网络播放路径从**零覆盖**变成 11 个可回归用例。

## 1. 背景：这是一块真正的零覆盖区

不是"覆盖得不够细"，是**一次都没跑过**：

| 事实 | 核实结果 |
|---|---|
| `VENetworkSource` / `VEHttpDataSource` / `VEBufferedDataSource` 代码在 | `core/net/` 四个类约 1000 行，`VESourceRegistry` 已注册 http/https scheme |
| 设备上播过网络流吗 | **从没有**。这三个类没有任何一次真机执行记录 |
| `vstarve`/`astarve` 计数器 | 在 `VEPerfStats.h:376` 的 VESTAT 行里，但**从未被非零值验证过**（本地文件几乎不饥饿，1080p 实测恒为 0） |
| buffering 事件链路 | `VEBufferedDataSource` → `VENetworkSource::onBufferingEvent` → `postNotify` → `VEPlayer.cpp:1634` → Java，**从未被真实事件穿过** |
| 错误链路 | 从未被真实错误穿过（喂给播放器的一直是好文件） |
| 内存维度 | **零**。核实 `VEPerfStats.h` 中 56 处 "memory" 里 **55 处是 `memory_order_relaxed`**，剩 1 处是注释里的英文单词。没有任何 RSS/fd 采集 |

顺带解锁一个长期挂起项：**decoder-starve-wake-dedup 步骤4**（饥饿唤醒去重的正向验证）
卡了很久，缺的正是"一个能持续制造饥饿的源"。本 feature 完成后它可以直接开工 ——
这是本 feature 的一个明确交付物，不是副作用。

### 1.1 一处必须更正的前提（核实代码后得出）

需求描述里写"现在测 FFmpeg http、将来换自定义 IO 同一套照跑"。
**读代码后发现：自定义 IO 已经是当前实现，不是将来。**

`VENetworkSource::openInput()` 的实际做法（`core/net/VENetworkSource.cpp:85-127`）：

```
VEHttpDataSource(自己写的 socket + BIO/SSL + Range + 重定向)
  → VEBufferedDataSource(独立预取线程 + 32MB 环形缓存 + 水位判定)
    → avio_alloc_context(自定义 read/seek 回调)
      → ctx->pb = mAvio; ctx->flags |= AVFMT_FLAG_CUSTOM_IO
        → avformat_open_input(&ctx, nullptr, ...)   // 路径传 nullptr
```

路径传 `nullptr`、置 `AVFMT_FLAG_CUSTOM_IO`，意味着 **FFmpeg 的 http 协议根本没被用到，
FFmpeg 只做 demux**。记忆 [[source-network-io-not-ffmpeg]] 描述的那个"未来架构"，
在这条路径上**已经落地了**。

这让本 feature 的价值判断整体上移一档：

- 原以为是"测一个 FFmpeg 内置协议"（FFmpeg 自己有测试，我们只测集成）
- 实际是"**约 1000 行本项目手写的 HTTP 客户端 + 预取缓存 + 水位状态机，
  唯一一次被执行**"。HTTP 状态码处理、206/200 分支、重定向、Range 重开、
  环形缓冲绕接、预取线程与消费线程的条件变量配合 —— 全部零验证。

**因此本套服务器与判据仍然是"对实现无关"的**（判据只谈"限速多少 → 该不该饥饿"，
不谈实现），将来若把 `VEHttpDataSource` 换成别的下载器，同一套照跑。
但它的定位从"未来 feature 的前置"变成"**当前已有实现的首次验证**"。

## 2. 三个架构决定的"为什么"

### 2.1 adb reverse 而非 WiFi

- **确定性**：局域网引入路由器、信道拥塞、其它设备流量三个不可控变量。
  本项目的判据是"限速到码率的 0.6 倍，`vstarve` 必须非零"，
  这类判据要求带宽是**我们设定的那个值**，不是"大约那个值"。
- **私人素材不出机器**：服务器**只绑 `127.0.0.1:8188`**，
  `adb reverse tcp:8188 tcp:8188` 让设备的 localhost:8188 打通到宿主机回环。
  局域网上任何一台机器都扫不到这个端口。
- 副作用（需在报告里声明）：`adb reverse` 走 USB，
  **它的带宽和延迟特性不等于真实 WiFi/4G**。所以本 harness 的结论是
  "**播放器在给定的供给曲线下如何反应**"，不是"在真实网络下如何"。
  这正是我们要的：供给曲线由服务器注入，可复现。

### 2.2 服务器请求日志（字节级）是独立交叉源 —— 这是本设计的核心

decoder-test-redesign §4 定了准入门槛：**单一来源的判据不予采纳**。
网络场景过去测不了，很大程度上就是因为没有第二个来源 ——
播放器说"我饥饿了 3 次"，除了它自己没人能证实。

服务器日志提供了这个来源，而且是**在被测进程之外**：

| 播放器侧（进程内） | 服务器侧（进程外） | 能判定什么 |
|---|---|---|
| `vstarve`/`astarve` 每秒增量 | 每秒实际送出字节数 | 饥饿是不是真的发生在供给不足的那几秒 |
| BUFFERING_START/END 事件时刻 | 限速/断流注入的起止时刻 | 事件链路延迟多久、有没有丢事件 |
| `fps` 塌陷区间 | 字节速率塌陷区间 | 塌陷是供给导致的，还是解码器自己的问题 |
| seek 后首帧时刻 | 收到的 Range 请求的 offset 与时刻 | seek 触发了几次重连、请求的位置对不对 |
| 播放总时长 | 累计送出字节 | 有没有重复下载（缓存/重定位逻辑的效率） |

没有这一列，`vstarve` 只能自己证明自己。这与 observability-instrumentation
一轮的教训完全同源 —— 那一轮新增指标第一版**错了七次**，
七次全是"字段名承诺 A、实际度量 B"，全部靠外部数据发现，没有一次是审代码发现的。

### 2.3 素材不入库

素材是用户私人文件（12G / 23 个 mp4 / 48 张图）。

- serving 目录、manifest **均不进 git**（`.gitignore` 明确排除）
- 仓库只进：`scripts/media-server.py`、`scripts/scan-assets.sh`、
  `scripts/gen-probe-asset.sh`、以及 harness 的网络扩展
- 报告里引用素材一律用 **manifest 里的指纹**（分辨率/编码/帧率/码率/时长/轨道构成），
  不写文件名以外的任何内容，不附带截图中的画面内容

## 3. 素材：真实素材提供合成矩阵造不出的轴

已用 `ffprobe` 普查 `~/Downloads`（23 mp4 / 12G / 48 图），挑选集如下。
每一条都是 `gen-test-assets.sh` 的合成矩阵**没有**的真实轴：

| 素材 | 实测特征 | 验什么 | 合成矩阵为什么造不出 |
|---|---|---|---|
| lv_0_20250904221155.mp4<br>lv_0_20250904221453.mp4 | 竖屏 1080×1920，**VFR**（tbr 1000000/1），11.3s | 竖屏/旋转 + 变帧率 | 合成一律 CFR；旋转 matrix 手工造不真 |
| VID_20250904_081827.mp4 | 真实 **HEVC 4K** 3840×2160，VFR 90000/3001，12.8s | 手机实拍 HEVC，对照合成 HEVC | 实拍码流的 SPS/PPS、参考帧结构与 x265 默认档不同 |
| VID_20250904_082411.mp4 | 真实 HEVC 1080p30，11.3s | 同上 | 同上 |
| test.mp4 | 1280×720，23.976fps，**7877s（2.2h）**，**双音轨** | 长片 seek、selectTrack、长稳 | 双音轨 + 超长时长 |
| test2.mp4 | 1920×1080，23.976fps，**3218s（53min）** | **网络长稳主力** | 长稳需要真实长素材 |
| 截图 2026-08-16 20.57.24.mp4 | **无音轨**，60fps，**1878×1180**（非标准宽高），1.47s | 无音轨网络路径、奇宽高对齐、**极短时长边界** | 非 16 对齐的宽高是真实录屏产物；1.47s 逼近"还没起播就 EOS" |
| 截图 2026-08-16 20.57.36.mp4 | **无音轨**，60fps，**2596×1436**，27.8s | 同上 | 同上 |
| png/jpg（48 张） | 非媒体内容 | 喂给播放器 → **优雅失败** | 错误链路从未被真实错误穿过 |

另需一个**合成探针素材**（`scripts/gen-probe-asset.sh` 生成，可入库脚本、产物不入库）：
四角固定色块（已知 RGB）+ 中央 `drawtext` 烧录帧号。用于：

- **画面正确性断言**：`screencap` 取四角像素，比对期望 RGB —— 能抓花屏/绿屏/
  色彩空间错（BT.601/709 × full/limited）/画幅拉伸/旋转/上下颠倒
- **A/V 外部地面真值**：烧录帧号给出"画面此刻在第几帧"，与音频侧对照

### 3.1 VFR 必须在 manifest 里打标记（这是步骤1 的硬性要求）

两个 lv_* 素材与两个 HEVC 素材是 VFR。现有判据里有
"fps ≈ `r_frame_rate`" 这一类，**在 VFR 素材上会稳定误报**。

规则：manifest 记录 `is_vfr`（由 `r_frame_rate` 与 `avg_frame_rate` 是否一致、
以及 tbr 是否异常判定），**帧率类判据对 `is_vfr=true` 的素材自动豁免**，
改用"帧率不为 0 且渲染帧数与 `nb_frames` 同量级"这一较弱判据，
并在报告里显式标注"该素材 VFR，帧率判据已豁免"。

这条与"测量素材规范"里那条 **"任何以素材结构为前提的判断必须附 ffprobe 实际输出"**
是同一件事 —— 本项目已在 seek 精度上因"按编码参数推算"连错三次。

## 4. 服务器设计（scripts/media-server.py）

单文件 Python 3 标准库实现，无第三方依赖。

### 4.1 数据面

- `GET /media/<name>` 供给 serving 目录下的素材
- **完整 Range 支持**：`bytes=N-`、`bytes=N-M`，返回 206 + `Content-Range`；
  `Accept-Ranges: bytes`。`--no-range` 模式下**不返回** `Accept-Ranges`
  且对 Range 请求返回 200 全量（模拟不支持 Range 的服务端）
- `Content-Length` 必须正确（`VEHttpDataSource` 依赖它，`avioSeek` 的
  `AVSEEK_SIZE` 分支要靠 `mDataSource->size()`）

### 4.2 注入面（三种）

| 注入 | 参数 | 实现 | 对应场景 |
|---|---|---|---|
| 限速 | `rate=<bytes/s>` | 发送循环里按令牌桶 sleep | throttle-above / throttle-below / longrun |
| 首字节延迟 | `ttfb=<ms>` | 响应头发出前 sleep | slow-ttfb |
| 断流 | `stall=<ms>` 或 `stall=forever` | 传输中途停止写、保持连接（**不是关闭**） | stall-recover / stall-forever |

断流用"保持连接但不发数据"而不是"关闭连接"，理由：这两种是**不同的故障**，
播放器的反应路径也不同（前者走读超时，后者走 EOF/EIO）。
v1 先做前者（更难、更接近真实弱网）；关闭连接留作后续扩展。

### 4.3 控制面

`POST /control` 接受 JSON，运行期改注入参数（不重启服务器）。
这样 stall-recover 才能做到"播放中第 20 秒断流、第 25 秒恢复"这种带时序的注入。

### 4.4 日志面（不可省略，见 §2.2）

每个请求一行 + 每秒一行聚合，落 `test-reports/raw/<case>/server.log`：

```
REQ  t=<epoch_ms> method=GET path=/media/x.mp4 range=<start>-<end> status=206 conn=<id>
RATE t=<epoch_ms> conn=<id> sentBytes=<n> cumBytes=<n> throttle=<rate> inject=<none|stall|ttfb>
END  t=<epoch_ms> conn=<id> reason=<complete|client_close|abort> totalBytes=<n> durMs=<n>
```

`t=` 用 epoch 毫秒，**必须能与设备日志对齐** —— 步骤4 要解决时钟对齐
（记录一次 `adb shell date +%s%3N` 与宿主机时刻的偏移，写进 `env.txt`）。

## 5. 场景矩阵（11 个）

每个场景 = 一组服务器注入参数 × 一组判据。**每条判据都标注双来源**（§2.2）。

| # | 场景 | 服务器注入 | 主判据 | 交叉校验 |
|---|---|---|---|---|
| 1 | **baseline** | 无 | 与同素材本地播放对照，启播/fps/dropLate 无显著劣化 | 服务器累计字节 ≈ 文件大小 |
| 2 | **throttle-above** | rate = 码率 × 1.5 | 与 baseline 无显著差异，`vstarve`=0 | 服务器每秒字节稳定在码率附近（未触限速上限） |
| 3 | **throttle-below** | rate = 码率 × 0.6 | **`vstarve`/`astarve` 必须非零** —— 计数器首次真实验证；BUFFERING_START/END 成对出现且到达 Java 层 | fps 塌陷的秒区间 与 服务器字节速率塌陷区间**对齐** |
| 4 | **slow-ttfb** | ttfb = 2000ms | 2s 等待落在启播 **T1/T2 段**，不被算进解码耗时 | 服务器首字节时刻 与 设备 T1→T2 分段边界对齐 |
| 5 | **stall-recover** | 播放中 stall 5s 后恢复 | 饥饿时长直方图有样本；恢复后**不花屏**；时间线连续无缺号 | 服务器 stall 起止 与 BUFFERING_START/END 时刻对齐 |
| 6 | **stall-forever** | stall 不恢复 | 错误/超时链路**触达 Java 层**、不 ANR、状态机留痕完整 | 服务器 END reason=client_close（证明播放器主动放弃而非挂死） |
| 7 | **no-range** | `--no-range` | seek 降级行为**明确**：要么明确失败要么回退到从头读，**不能挂死** | 服务器只收到 200 无 206；无重复全量下载风暴 |
| 8 | **seek-http** | Range 开 + `seekPercents` | `seekTrace` 三阶段与本地对照，网络成本可见 | 服务器 Range 请求的 offset 与 seek 目标位置**大致对应** |
| 9 | **bad-content** | 供给一张 jpg | demux open **优雅失败** + 明确错误事件到 Java | 服务器 200 正常送完（证明失败发生在播放器侧，不是传输问题） |
| 10 | **longrun** | test2.mp4（53min）× rate=码率×1.2 × 循环 | RSS/fd **时间线斜率**做泄漏检测；无累积性 dropOverflow | 服务器连接数不单调增长（连接泄漏的独立证据） |
| 11 | **probe-visual** | 探针素材 | `screencap` 四角像素断言 + 中央帧号 | 帧号 → A/V 外部真值 |

### 5.1 一个必须先算清楚的量：time-to-starve

`VEBufferedDataSource::Config` 默认（`VEBufferedDataSource.h:26-33`）：
`cacheBytes=32MB`、`startWaterBytes=2MB`、`lowWaterBytes=256KB`、`resumeWaterBytes=1MB`。

**throttle-below 会不会真的饥饿、多久饥饿，是可以算的**：
供给 0.6×码率、消费 1.0×码率，起播水位 2MB，
`time_to_starve ≈ (startWater − lowWater) / (bitrate × 0.4)`。
1080p 约 5Mbps（625KB/s）时 ≈ **(2MB−256KB)/250KB/s ≈ 7s**。可接受。

**但同一个算式对 stall-recover 给出一个反直觉的结论**：
不限速时预取会把 32MB 缓存填满，1080p 下 32MB ≈ **50 秒**的存量。
此时"断流 5 秒"**什么都不会发生** —— 没有饥饿、没有 buffering 事件，
用例会以 PASS 结束却**什么也没验证到**（这正是零值最危险的那一类：
"没发生"和"没在测"长得一模一样）。

所以：**stall 类场景必须叠加限速**（让缓存维持在浅水位），
或 stall 时长必须超过缓存排空时间。harness 在步骤4 要**由 manifest 的
`bit_rate` 自动算出该素材的缓存排空时间并写进 `env.txt`**，
场景参数不满足条件时**直接拒绝执行并报 INCONCLUSIVE**，不允许跑出一个空洞的 PASS。

### 5.2 判据的通用底线

沿用 decoder-test-redesign §4 与 perf-metrics 7d：
**实测值与交叉校验值缺一即 INCONCLUSIVE，不算 PASS。**
网络场景尤其容易出现"注入根本没生效但播放很顺，于是全绿"——
服务器日志是唯一能发现这一点的地方。

## 6. 与既有工作的关系

| 既有工作 | 关系 |
|---|---|
| perf-metrics 步骤7（run-benchmark.sh / gen-report.py / 报告格式） | **直接复用并扩展**，不另起一套。步骤4 给 harness 加 `--url` 与场景编排 |
| decoder-test-redesign §2 故障注入 | **互补而非重复**：那里注入的是**解码器**故障（编译期开关），这里注入的是**供给**故障（服务器侧，不动被测代码）。服务器侧注入不需要改 native，先落地 |
| decoder-starve-wake-dedup 步骤4 | **被本 feature 解锁**。throttle-below 是它等的那个"能持续饥饿的源" |
| high-perf-player Phase 3（网络源） | 本 feature 是它的首次真机验证 |
| observability-instrumentation（VESTAT 逐秒时间线） | 判据的主数据源。本 feature 步骤5 给它补 RSS/fd 两列 |
| 记忆 [[source-network-io-not-ffmpeg]] | 见 §1.1 —— 那个架构**已经落地**，本 feature 是它的验证，不是它的前置 |

## 7. 风险

| 风险 | 应对 |
|---|---|
| **私人素材泄露** | 服务器只绑 `127.0.0.1`（写死，不提供 `--host`）；serving 目录与 manifest 不入库；报告只引用指纹 |
| **网络路径第一次就挂** | 概率很高（1000 行代码从没执行过）。**步骤3 单列一个 smoke**，失败即转 bug 排查 —— 这是**发现**不是阻塞，本 feature 的第一价值就在这里 |
| **VFR 打破帧率判据** | manifest 打 `is_vfr` 标记，判据参数化并在报告里声明豁免（§3.1） |
| **注入未生效却全绿** | 服务器日志作为强制交叉源；缓存排空时间不满足条件时拒绝执行（§5.1） |
| **2.2h 素材拖慢矩阵** | test.mp4 只用于 seek 与长稳，不进常规矩阵 |
| **时钟对齐失败导致"区间对齐"类判据失效** | 步骤4 显式记录设备与宿主机的时钟偏移，写进 `env.txt`；偏移未采到则相关判据判 INCONCLUSIVE |
| **logcat 300 行配额**（ColorOS） | 记忆 [[coloros-logcat-quota]]：长稳场景必然超配额。longrun 必须落盘读回，不能只看 logcat；harness 的缺号检测已有，须对网络场景同样生效 |
| 每个新维度自带覆盖率对账 | 延续 VEGAUGE 模式：服务器送出字节 vs 播放器消费字节，两者长期偏离即说明有重复下载或缓存失效 |

## 8. 非目标

- 不测真实网络（WiFi/4G/弱信号）—— 那是不可复现的，本 harness 只测**给定供给曲线下的反应**
- 不测 HTTPS 证书链（`VEHttpDataSource` 有 SSL 分支，但回环自签证书引入的问题多于收益，留作后续）
- 不测流媒体协议（HLS/DASH/RTMP）—— 播放器目前不支持
- 不做编码正确性比对（PSNR/SSIM），与 decoder-test-redesign §1 非目标一致
