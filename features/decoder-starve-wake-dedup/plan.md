# decoder-starve-wake-dedup 实施计划

> 设计文档: ../../docs/decoder-starve-wake-dedup-design.md
> 创建日期: 2026-08-08
> 缺陷来源: high-perf-player Phase 4（requestReadNotify 替换 10ms 轮询那批改动）

## 方案摘要

解码器一次上游饥饿会同时武装两个唤醒源（`requestReadNotify` 一次性通知 + 500ms `kStarveBackstopUs`
兜底重试），而 `kWhatDecode` 成功后自我续投，导致两条 `epoch` 相同、互相过滤不掉的并行自驱解码循环。
链路数**有界**（撞 credit 上限时全部退出，边沿触发的复活只投一条，每次 park 收敛回 1），
持续多条只发生在 credit 一直不满时，表现为多余链路空转耗 CPU。
修法：两个解码器各加 `int32_t mStarveGen`，饥饿时 `++mStarveGen` 并让两条唤醒消息都带该 gen，
`kWhatDecode` 里校验后 `++mStarveGen` 作废兄弟消息；不带 gen 的消息（自驱续投、credit 复活、start）一律放行。

## 实施步骤

1. **VEVideoDecoder 加代次守卫**
   - 目标：`VEVideoDecoder.h` 新增 `int32_t mStarveGen = 0;`；饥饿分支 `const int32_t starveGen = ++mStarveGen;`，
     通知消息与兜底消息均 `setInt32("starveGen", starveGen)`（保留原有 `epoch`）；
     `kWhatDecode` 在 `epoch` 与 `mIsStarted` 校验之后插入 gen 校验，不等则丢弃、相等则 `++mStarveGen`。
   - 验收：`./gradlew assembleDebug` 通过；不带 `starveGen` 的消息路径（`postDecode` 自驱续投、
     credit 复活、首次 start）代码上确认无 gen 字段、不受守卫影响。

2. **VEAudioDecoder 同构改动**
   - 目标：与步骤1 完全同构，字段名/日志文案/校验顺序保持一致，便于后续对读。
   - 验收：`./gradlew assembleDebug` 通过；两个解码器的守卫代码逐行可对照。

3. **无回归真机验证（1080p 硬解 / 1080p 软解 / 纯音频）**
   - 目标：三种素材播至 COMPLETED，渲染帧数、丢帧数、时长与修改前逐项一致。
   - 验收：帧数 278 / 287 / 0，丢帧 0，时长 9529 / 9529 / 10000ms。

4. **正向验证「两个唤醒源互斥」生效（持续饥饿场景）**
   - 目标：造出会持续饥饿的源，实测多余链路确实被消掉。
     - 先加一对可读回的计数 `starveCount` / `staleStarveWakeCount`（每解码器各一对），
       **写文件读回或挂 perf-metrics 面板，不得只靠 logcat**（测试机 300 行配额会冲掉）；
     - 用路径 (a)：`adb reverse` + 本机 HTTP 服务（必要时限速）喂网络源，
       顺带覆盖 high-perf-player Phase 3 至今未做的网络源真机验证；
     - 备选路径 (b)：临时调小 demux 缓冲水位（`maxTotalBytes` / `bufferedDurationTargetUs`），
       改测试用代码、验完必须回滚。
   - 验收：`starveCount > 0` 且 `staleStarveWakeCount > 0`，播放到 COMPLETED，丢帧不增加。
   - 备注：计数器落位需与 perf-metrics 步骤5 协商，避免各自造一套脚手架。
