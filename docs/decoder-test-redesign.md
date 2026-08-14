# 解码测试重新设计（decoder-test-redesign）

> 创建日期: 2026-08-14
> 关联: docs/test-console-ui-design.md（性能面板与指标）、features/high-perf-player（Phase 2 硬解与 fallback）

## 0. 为什么要重做

现在的解码测试**只能测正常路径**。这一轮所有验证都是同一个动作：打开一个文件，
看它能不能播。而解码器在真实设备上真正会出问题的地方，一个都测不了：

| 真实故障 | 现在能测吗 |
|---|---|
| 硬解建链失败 → 回退软解 | **不能**。`VE_INFO_DECODER_FALLBACK` 自 Phase 2 起从未被触发过一次 |
| codec 被其它应用抢占 / 实例耗尽 | 不能 |
| 不在白名单的编码（VP9/AV1/MPEG4） | 不能，没有素材也没有判据 |
| 播放中分辨率变化 | 不能 |
| 损坏码流 / 丢包 | 不能 |
| 硬解中途报错 | 不能 |

**运行期 fallback 至今零验证**，这是 high-perf-player Phase 2 的硬指标里唯一空白的一条。
它不是"还没排上"，而是**没有触发手段**——只能等一台恰好不支持某编码的设备，
或者一个恰好损坏的文件。这不叫测试。

还有一个更隐蔽的问题：**判据依赖的指标本身可能是错的**。这一轮修了四个"名字与
实际度量不符"的读数（硬解解码耗时实为背压等待、软解解码耗时漏了 send_packet、
首帧上屏实为同步等待、丢帧数只统计了一种）。如果解码测试的通过判据建立在这类
数字上，测试会给出自信而错误的结论。

## 1. 设计目标

1. **可触发**：每一条故障路径都有确定性的触发手段，不依赖运气
2. **可判定**：通过/失败由指标自动判定，不靠人看数字"感觉正常"
3. **可复现**：同一命令在同一素材上必须给出同样结论
4. **判据自身可信**：每条判据至少两个独立来源交叉验证

非目标：不做编码正确性（PSNR/SSIM）比对，那是编解码器自身的事，不是播放器的事。

## 2. 核心：故障注入

没有这一层，上面那张表里的每一行都测不了。

### 2.1 注入点

在 `VEVideoDecoderFactory` 与 `VEMediaCodecVideoDecoder` 上开一组**仅调试构建生效**
的注入开关：

| 开关 | 注入位置 | 用于验证 |
|---|---|---|
| `failHwCreate` | `createDecoderByType` 返回前置空 | 建链期回退软解 |
| `failHwConfigure` | `AMediaCodec_configure` 返回前改错误码 | 配置期回退 |
| `failHwAfterFrames=N` | `dequeueOutputBuffer` 第 N 帧后返回错误 | **运行期**回退（最难触发、最该测） |
| `forceUnsupportedCodec` | 白名单判定处强制未命中 | 不支持编码走软解 |
| `corruptPacketEvery=N` | 送包前打乱第 N 个包的载荷 | 损坏码流的容错 |

注入开关必须满足两条纪律：

- **编译期隔离**：`#if VE_ENABLE_FAULT_INJECTION`，Release 构建里连代码都不存在。
  播放器的故障注入一旦能在生产包里被打开，就是一个可被利用的稳定性开关。
- **状态可见**：注入生效时性能面板必须显示"注入中"，否则一次忘记关闭的注入
  会让后续所有测试结论作废，而且极难发现。

### 2.2 运行期 fallback 的判定链

这是最有价值的一条用例，判定必须逐环检查，缺一环都算失败：

```
failHwAfterFrames=100 → 播放 1080p h264
  ① 前 100 帧 decodePath = hardware，codecLatencyMs 有样本
  ② 第 100 帧后收到 VE_INFO_DECODER_FALLBACK 事件（事件流可见）
  ③ decodePath 变为 software，videoDecodeMs 开始有样本
  ④ 播放**不中断**：dropLate 增量有上限，位置持续推进，无 ERROR
  ⑤ 回退后不再反复尝试硬解（decodePath 稳定在 software）
```

第 ⑤ 条容易被忽略：回退如果会反复抖动，表现是周期性卡顿，比不回退更糟。

## 3. 素材矩阵

这一轮最贵的教训是：**合成小素材会系统性掩盖瓶颈**。640×360 上 `find_stream_info`
只要 2~4ms，1080p 上是 133~145ms，跃升为启播第一大项——换素材后变的不是一个数字，
是瓶颈的排序。所以素材矩阵必须显式声明每个素材"用来验什么"，禁止拿一个素材下
通用结论。

| 素材 | 参数 | 验什么 |
|---|---|---|
| base-h264-1080p | 1920×1080 30fps 20Mbps + AAC | 主基线，所有性能对照以它为准 |
| base-hevc-1080p | 1920×1080 30fps HEVC | 第二编码路径、白名单第二项 |
| high-4k | 3840×2160 30fps | 软解天花板、硬解 configure 规模效应 |
| high-fps | 1920×1080 60fps | 同步余量与上屏间隔抖动的压力点 |
| tiny-360p | 640×360 25fps | **只用于验证采集点完整性，不得作性能基线** |
| no-audio | 无音轨 | 时钟起锚退化路径 |
| audio-only | 纯音频 | 不建视频链路 |
| short-audio | 视频 10s + 音频 3s | EOS 双链路判定 |
| nonzero-start-pts | 首帧 pts ≠ 0（带 edit list） | 首帧免等待与时钟锚定 |
| res-change | 播放中分辨率切换 | 纹理/codec 重配 |
| unsupported | VP9 或 AV1 | 白名单未命中走软解 |
| corrupt | 人为破坏若干帧 | 容错与不崩溃 |

前四个是性能基线素材，后面是行为素材。**性能结论只能来自前四个。**

## 4. 判据：用指标自动判定

现有指标已足以支撑客观判定，不需要人看画面。每条判据标注它依赖的指标与
**交叉校验来源**——单一来源的判据不予采纳。

| 判据 | 主指标 | 交叉校验 |
|---|---|---|
| 硬解真的在用硬解 | `decodePath=hardware` | `codecLatencyMs` 有样本 且 `videoDecodeMs` n=0 |
| 软解真的在解码 | `videoDecodeMs` p50 > 0 | `vdec_thread` CPU 与 p50×fps 同量级 |
| 解码跟得上 | `dropLate = 0` | `syncMarginMs` p95 > 0 |
| 谁是瓶颈 | `videoCreditPark` | 线程 CPU 分布 |
| codec 产能足够 | `codecLatencyMs` 不持续增长 | 队列峰值不触顶 |
| 播放平顺 | `presentIntervalMs` p95−p50 < 帧间隔的 30% | `dropLate = 0` |
| 起播链路完整 | 启播分段之和与总耗时差 < 5ms | 各段均非 null |

**注意 `dropLate=0` 不等于"没丢帧"**：丢帧分四类，`stale` 与 `seekCatchup` 是正常的，
`overflow` 出现即 bug。判据必须指明是哪一类。

## 5. 自动化

### 5.1 一切经 intent 驱动

已有 `source` / `autoplay` / `software` / `vulkan`，需补：

```
--ez faultHwCreate true          故障注入
--ei faultHwAfterFrames 100
--es seekPercents 10,50,90       自动 seek 序列
--ei playSeconds 15              播多久后自动收尾
--es caseName fallback-runtime   报告里的用例名
```

补齐后单条命令即可跑完一个用例，不需要点屏幕。这是把解码测试从"手工点"变成
"可回归"的前提。

### 5.2 报告

复用跑分（perf-metrics 步骤7）的报告格式，每个用例输出：

- 环境指纹（机型/系统/素材参数/三个策略开关/渲染后端/音频后端/**注入状态**）
- 判据逐条 PASS/FAIL 及其实测值与交叉校验值
- **逐秒时间线**（帧率、丢帧分类、CPU、队列水位、A/V 偏移）——聚合值看不出
  "某一秒全塌了"，而 fallback 恰好是一次瞬时事件

## 6. 与既有工作的关系

- 依赖 perf-metrics 的指标体系（已完成第一梯队五项）与步骤7 的报告
- 填补 high-perf-player Phase 2 "运行期 fallback 零验证" 这个长期空白
- 素材矩阵与 startup-cpu-opt 的"测量素材规范"同源，两处必须一致

## 7. 风险

| 风险 | 应对 |
|---|---|
| 故障注入泄漏到生产包 | 编译期隔离，Release 构建不含代码；面板显示注入状态 |
| 注入忘关导致后续结论作废 | 每次 prepare 后自动复位；报告头部记录注入状态 |
| 判据建立在错误指标上 | 每条判据强制双来源交叉校验（见 §4） |
| 素材矩阵膨胀到无人维护 | 每个素材必须写明"验什么"，验不到东西的删掉 |
| 4K/60fps 素材体积大 | 只留 10 秒片段；用 ffmpeg 脚本生成而非入库 |
