# demux-buffering 实施计划（demux 缓冲策略重构）

> 设计文档: ../../docs/demux-buffering-design.md
> 创建日期: 2026-07-24

## 方案摘要

VEDemux 的 park 条件「任一队列满就停整个读循环」在单一交织读取器（av_read_frame）下
造成队头阻塞：视频软解慢填满视频队列 → 读循环停 → 音频被抽干断音。
改为 ffplay 式策略：全局字节封顶（16MB，唯一硬停防 OOM）+ 每路各自缓冲够（1s 时长 / 25 包）才停，
任何单路都不再单独触发 park。队列上限由「计数节流」降级为防失控兜底（~2048）。

## 实施步骤

1. **队列字节/时长记账** — `VEPacket.h` 加 `int64_t mDurationUs` + set/get；
   `VEPacketQueue.h/.cpp` 加 `mTotalBytes` / `mTotalDurationUs`（put 加 / get 减 / clear 归零，全在已有 mutex 内）、
   getter `getTotalBytes()` / `getDurationUs()`；`mMaxSize` 抬到 ~2048 降级为防失控兜底。
   验收：`./gradlew assembleDebug` 零警告；记账加减严格锁在队列类内。独立 commit。

2. **shouldParkRead 策略** — `VEDemux.cpp` `onRead` 算 pts 时顺手
   `setDurationUs(av_rescale_q(pkt->duration, streamTimeBase, AV_TIME_BASE_Q), <=0 存 0)`；
   两段 per-queue 满检查替换为 `if (shouldParkRead()) return VE_NO_MEMORY;`；
   `scheduleContinueReadIfNeeded` 唤醒判据改用 `!shouldParkRead()`；
   新增 `shouldParkRead()` / `streamHasEnough()` helper + 常量（kMaxTotalBytes / kBufferedDurationTargetUs / kMinPackets）。
   验收：`./gradlew assembleDebug` 零警告。独立 commit。

3. **捆绑低风险修正** — `mDuration` 补 `AV_NOPTS_VALUE` 判空；`mFps` 补 `den==0` 判零；
   选视频流跳过 `AV_DISPOSITION_ATTACHED_PIC`；`read()` 补 `mAudioPacketQueue` 判空；
   删死枚举 `kWhatEOS` / `kWhatResume`。
   验收：`./gradlew assembleDebug` 零警告。独立 commit。

## 遗留（挂起）

- 真机回归：用户指示不上真机，全部步骤完成后补测（软解 1080p 断音、4K 内存峰值 ~16MB、起播预缓冲时延）。
