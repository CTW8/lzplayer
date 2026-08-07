# data-model-fixes 设计文档

> 创建日期: 2026-07-30
> 范围: demux → decoder → render 数据流转模型缺陷批量修复

## 背景与目标

对 LZPlayer 核心数据链路（解复用 → 解码 → 渲染）做了一次 code review，发现 6 项数据模型缺陷，按严重度排序后统一立项修复。这些缺陷涉及类型不一致、空指针解引用、静默丢包、未初始化、坏味道、缺兜底，分别会导致 AVSync 时钟错乱、SIGSEGV、解码花屏/缺音、UB 等问题。本方案目标是逐项、有序地消除这些缺陷，使数据模型在类型、生命周期、容量边界上保持一致与健壮。

## 涉及模块

- `lzplayer_core/src/main/cpp/core/VEFrame.h`
- `lzplayer_core/src/main/cpp/core/VEPacket.h`
- `lzplayer_core/src/main/cpp/core/VEAudioDecoder.cpp`
- `lzplayer_core/src/main/cpp/core/VEVideoDecoder.cpp`（参考对比）
- `lzplayer_core/src/main/cpp/core/VEDemux.cpp`（VEPacketQueue / shouldParkRead / putPacket）
- `lzplayer_core/src/main/cpp/core/VEVideoDisplay.cpp`
- `lzplayer_core/src/main/cpp/core/VEAudioRender.cpp`

## 缺陷清单与修复方案（按严重度排序）

### 缺陷1（高）：VEFrame::getPts() 返回类型与成员不一致

- 位置：`core/VEFrame.h:82`，`uint64_t getPts()`，成员 `timestamp` 为 `int64_t`，`setPts(int64_t)`。
- 后果：帧 pts == `AV_NOPTS_VALUE`（`INT64_MIN`）时，`getPts()` 返回 `uint64_t ≈ 9.2e18`，消费方 `VEVideoDisplay.cpp:371 updateVideoPts`、`VEAudioRender.cpp:264 updateAudioPts` 把"无时间戳"误当成"进度跳到极大值"，AVSync 时钟错乱、丢帧/停摆。即便值非负，用 `uint64_t` 表达 pts 本身也是错误类型。
- 修复：`getPts()` 返回 `int64_t`，与 `setPts` / `timestamp` 对齐。检查所有调用点是否依赖无符号语义（预期无依赖，pts 比较与 updateVideoPts/updateAudioPts 均按有符号处理）。

### 缺陷2（高）：音频重采样 VEFrame 构造失败后未判空即 memcpy，空指针解引用

- 位置：`core/VEAudioDecoder.cpp:334-345`。`std::make_shared<VEFrame>(...)` 构造内 `av_frame_get_buffer` 可能失败 → `mFrame = null`（VEFrame 构造函数失败时只 `av_frame_free` 不 return/throw，返回半残对象）。随后 `memcpy(audioFrame->getFrame()->data[0], ...)` 中 `getFrame()` 返回 null → SIGSEGV。
- 对比：视频路径 `convertToYuv420p`（`VEVideoDecoder.cpp:324`）有判空并返回 `nullptr`，`onDecode:393` 处理了 null；音频路径不对称，缺这层防护。
- 修复：音频路径构造后加判空：`if (audioFrame->getFrame() == nullptr || audioFrame->getFrame()->data[0] == nullptr) { 释放 out_data; return VE_UNKNOWN_ERROR; }`。

### 缺陷3（中）：VEPacketQueue 满时静默丢包，shouldParkRead 不查队列深度上限

- 位置：`shouldParkRead`（`VEDemux.cpp:167-178`）只看 `totalBytes >= 16MB` 和两路 `streamHasEnough`（25 包 / 1s），不看 `queue.size()` vs `kQueueBackstopPackets(2048)`。`putPacket`（`VEDemux.cpp:546-551`）put 返回 false 时只 `ALOGW("queue full, packet dropped")` 丢包。
- 后果：单路小包（低码率音频）堆积到 2048 但总字节远未到 16MB 时 put 满 → 丢包 → 下游解码花屏/缺音。生产侧本应满则 park 等 credit 回流。
- 修复：`shouldParkRead` 增加 `mAudioPacketQueue->getDataSize() >= kQueueBackstopPackets || mVideoPacketQueue->getDataSize() >= kQueueBackstopPackets` 作为 park 条件之一，让 2048 成为停读门槛而非丢包门槛。

### 缺陷4（低）：VEPacket::pts/dts 成员未初始化

- 位置：`core/VEPacket.h:73-75`，`int64_t pts;`、`int64_t dts;` 未初始化（`durationUs` 已初始化为 0）。VEFrame 已初始化（`timestamp=0`、`dts=0`），不一致。
- 后果：默认构造后未 `setPts` 就 `getPts` 是 UB。
- 修复：`int64_t pts = 0; int64_t dts = 0;`。

### 缺陷6（低）：渲染侧 mFrames(deque) 无深度兜底

- 位置：`VEAudioRender` / `VEVideoDisplay` 的 `mFrames` 是 `std::deque<pair<frame, reply>>`，`queueFrame` 时不检查上限，深度完全靠 decoder 侧 credit 单一防线。
- 修复：`queueFrame` 入队时若 `mFrames.size()` 超阈值，丢弃最旧帧并照常回执。低风险兜底。

### 缺陷5（低，可暂缓）：音频重采样手动改写 AVFrame->linesize[0]

- 位置：`core/VEAudioDecoder.cpp:349-351` 把 `av_frame_get_buffer` 给出的对齐 linesize 改写成紧凑字节数，破坏 AVFrame "linesize 是对齐行距" 不变量。当前不泄漏（SLES 兼容、`av_frame_free` 按 data 释放），但脆弱坏味道。
- 修复：建议不改写 `linesize[0]`，直接用 `out_data` 的 `out_buffer_size` 喂 SLES；或在 VEFrame 上另存 payload bytes 字段。本项低优先，可暂缓，标注清楚。

## 实施顺序

按严重度由高到低实施，每个缺陷一个步骤：

1. 步骤1 = 缺陷1（getPts 类型）
2. 步骤2 = 缺陷2（音频 memcpy 空指针）
3. 步骤3 = 缺陷3（队列静默丢包）
4. 步骤4 = 缺陷4（pts/dts 初始化）
5. 步骤5 = 缺陷6（mFrames 兜底）
6. 步骤6 = 缺陷5（linesize 改写，低优先 / 可暂缓）

每步完成后编译验证（`:lzplayer_core:externalNativeBuildDebug`）；涉及播放核心的改动在全部完成后整体做一次真机回归（用 lzplayer-test-expert 跑播放/暂停/seek/EOS 基本链路）。

## 风险与依赖

- 缺陷1改返回类型属 ABI 范畴内的接口变更，需排查所有调用点；预期消费方均按有符号使用，但须逐一确认。
- 缺陷3改 park 条件会改变 demux 读节奏，可能影响现有缓冲行为，需结合 demux-buffering / demux-nuplayer-refactor 的回归一起验证。
- 缺陷6的兜底丢弃最旧帧是行为变更，需确认不影响 AVSync 语义（预期被丢弃帧本身已是超期帧）。
- 真机回归依赖 lzplayer-test-expert agent 与设备可用性；若回归挂起，按既有 feature 惯例记录遗留项。
