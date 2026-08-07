# demux-buffering 进度（demux 缓冲策略重构）

> 最后更新: 2026-07-24
> 总体状态: Done

## Done

- [x] 步骤 1: 队列字节/时长记账（VEPacket.h 加 mDurationUs + set/get；VEPacketQueue.h/.cpp 加 mTotalBytes/mTotalDurationUs 记账与 getter）(2026-07-24)
  - 构建通过零警告；commit acbcc93「demux-buffering 步骤1: 队列字节/时长记账」。
  - 调整：mMaxSize「抬到 ~2048 降级为兜底」移到步骤 2 执行 —— 它必须与 shouldParkRead（移除单路 park）同一步落地，否则队列在 100 处 put 失败丢包、数据丢失；步骤 1 保持纯累加、零行为改动更安全。
- [x] 步骤 2: shouldParkRead 策略（onRead 记 durationUs、park 判据换 shouldParkRead()、唤醒判据换 !shouldParkRead()、新增 helper + 常量；含 mMaxSize 抬到 ~2048 兜底移交）(2026-07-24)
  - 构建通过零警告；commit 9c8f191「demux-buffering 步骤2: shouldParkRead 策略, 消除队头阻塞」。
- [x] 步骤 3: 捆绑低风险修正（mDuration 补 AV_NOPTS_VALUE 判空、mFps 补 den==0 判零、选视频流跳过 AV_DISPOSITION_ATTACHED_PIC、read() 补 mAudioPacketQueue 判空、删死枚举 kWhatEOS/kWhatResume）(2026-07-24)
  - 构建通过零警告；commit 097887e。

## Doing

（无）

## Todo

（无）

## 遗留（挂起）

- 真机回归按用户指示挂起。全部步骤完成后补测重点（见 plan.md）：①软解 1080p + 音频是否还断 ②4K/高码率内存峰值守在 ~16MB ③起播预缓冲时延。
