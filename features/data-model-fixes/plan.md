# data-model-fixes 实施计划

> 设计文档: ../../docs/data-model-fixes-design.md
> 创建日期: 2026-07-30

## 方案摘要

对 demux → decoder → render 数据流转模型 code review 发现的 6 项缺陷做批量修复，按严重度排序逐项实施。涵盖类型一致性（pts 有符号化）、空指针防护（音频重采样 memcpy 判空）、队列停读门槛（2048 包作为 park 条件避免静默丢包）、成员初始化、渲染 deque 深度兜底，以及低优先的 linesize 改写坏味道。每步编译验证，整体完成后真机回归。

## 实施步骤

1. 缺陷1（高）：VEFrame::getPts() 返回类型 `uint64_t` → `int64_t`。改 `core/VEFrame.h:82`，与 `setPts`/`timestamp` 对齐，排查所有调用点是否依赖无符号语义。验收：编译通过，调用点语义正确，pts==AV_NOPTS_VALUE 时不再被误判为极大值。

2. 缺陷2（高）：音频重采样路径 VEFrame 构造后判空。在 `core/VEAudioDecoder.cpp:334-345` 构造后加 `if (audioFrame->getFrame()==nullptr || audioFrame->getFrame()->data[0]==nullptr) { 释放 out_data; return VE_UNKNOWN_ERROR; }`，对齐视频路径 convertToYuv420p 的防护。验收：编译通过，构造失败路径不再 SIGSEGV。

3. 缺陷3（中）：VEPacketQueue 停读门槛。在 `VEDemux.cpp shouldParkRead` 增加 `mAudioPacketQueue->getDataSize()>=kQueueBackstopPackets || mVideoPacketQueue->getDataSize()>=kQueueBackstopPackets` 作为 park 条件之一，使 2048 成为停读门槛而非丢包门槛。验收：编译通过，低码率小包场景不再静默丢包。

4. 缺陷4（低）：VEPacket::pts/dts 初始化。`core/VEPacket.h:73-75` 改为 `int64_t pts = 0; int64_t dts = 0;`，与 VEFrame、durationUs 一致。验收：编译通过，默认构造后 getPts/getDts 不再是 UB。

5. 缺陷6（低）：渲染侧 mFrames 深度兜底。VEAudioRender / VEVideoDisplay 的 queueFrame 入队时若 mFrames.size() 超阈值，丢弃最旧帧并照常回执。验收：编译通过，deque 深度有上限。

6. 缺陷5（低，可暂缓）：音频重采样 linesize[0] 改写坏味道。`core/VEAudioDecoder.cpp:349-351` 不改写 linesize[0]，改用 out_data 的 out_buffer_size 喂 SLES，或在 VEFrame 上另存 payload bytes 字段。验收：编译通过，AVFrame linesize 不变量保持；本步可暂缓，需用户确认是否本次实施。

7. 整体真机回归：用 lzplayer-test-expert 跑播放/暂停/seek/EOS 基本链路，确认数据模型改动不破坏核心链路；回归报告落 test-reports/。验收：回归通过或遗留项明确记录。
