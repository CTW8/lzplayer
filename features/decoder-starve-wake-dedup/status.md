# decoder-starve-wake-dedup 进度

> 最后更新: 2026-08-08
> 总体状态: Doing

## Done

- [x] 步骤 1: VEVideoDecoder 加代次守卫 (2026-08-08) — `VEVideoDecoder.h:137` 新增 `int32_t mStarveGen = 0;`；
  `VEVideoDecoder.cpp:480` 饥饿分支 `const int32_t starveGen = ++mStarveGen;`，`requestReadNotify` 通知消息与
  `post(kStarveBackstopUs)` 兜底消息均带 `starveGen`（原有 `epoch` 保留）；`kWhatDecode`（约 148~157 行）
  在 `epoch`→`mIsStarted` 之后插入 gen 校验，不等丢弃并打 `stale starve wake`，相等则 `++mStarveGen` 作废兄弟消息。
  不带 `starveGen` 的消息（`postDecode` 自驱续投 / credit 复活 / 首次 start）一律放行，正常解码循环行为未变。
- [x] 步骤 2: VEAudioDecoder 同构改动 (2026-08-08) — `VEAudioDecoder.h:133` + `VEAudioDecoder.cpp:138~143 / 449~457`，
  与视频侧逐行同构（字段名、日志文案、校验顺序一致）。`VEDemux::requestReadNotify`、`IMediaSource` 接口、
  credit 记账、渲染侧均未改动。
- [x] 步骤 3: 无回归真机验证 (2026-08-08) — 1080p 硬解 / 1080p 软解 / 纯音频 m4a 三种素材均播至 COMPLETED；
  渲染帧数 **278 / 287 / 0 与修改前完全一致**，丢帧 0，时长 9529 / 9529 / 10000ms 正确。
  **注意：本步只证明"没改坏"，不证明修复生效** —— 见 Todo 步骤4。

## Doing

（无）

## Todo

- [ ] 步骤 4: 正向验证「两个唤醒源互斥」生效（持续饥饿场景）— **本 feature 唯一未完成项，也是唯一阻塞转 Done 的事项**
  - 未验证原因：本地文件几乎不饥饿。1080p 两次实测**饥饿次数 = 0**（demux 预填足够），守卫代码未被执行；
    纯音频那次**饥饿 1 次但"被作废的兜底唤醒"计数为 0**，推断饥饿发生在流尾、解码器随后 EOS 停止，
    500ms 后兜底消息到达时先被 `!mIsStarted` 拦下，走不到代次校验。
  - 现有间接证据：纯音频那次真实饥饿之后仍播到 COMPLETED —— 若守卫误判正常通知为过期，该文件会卡死。
    故可判定**守卫没有误杀正常唤醒**，但"多余链路被消掉"缺正面证据。
  - 前置：当前代码**没有任何持久化的饥饿/去重计数器**（`grep -rn starve` 只命中两个解码器的 ALOGV）。
    需先加 `starveCount` / `staleStarveWakeCount`（每解码器各一对），**写文件读回**
    （`adb shell run-as com.example.lzplayer cat files/<path>`）或挂 perf-metrics 面板；
    测试机 OPPO PHK110 单进程 logcat 300 行配额，**不得只依赖 logcat**。
  - 路径：**(a) 接 HTTP 源（推荐）** —— `adb reverse` + 本机 HTTP 服务，必要时限速逼出持续饥饿；
    顺带覆盖 high-perf-player Phase 3 网络源至今零真机验证的空白。
    (b) 临时调小 demux 缓冲水位（`maxTotalBytes` / `bufferedDurationTargetUs`）—— 更快但改被测代码，验完须回滚。
  - 验收：`starveCount > 0` 且 `staleStarveWakeCount > 0`，播放到 COMPLETED，丢帧不增加。
- [ ] 代码提交（步骤1~3 的改动当前未提交；按 CLAUDE.md 需用户明确同意）。

## 备注

- **归属决策**：用户倾向并入 demux-nuplayer-refactor，最终**单列**。理由：双唤醒源由 high-perf-player
  **Phase 4** 引入（不是 refactor 引入）；refactor 的范围明确是"驱动模型内核行级不变"，其步骤5 回归的
  验收标准正是"确认驱动模型无回归"，塞入一处**故意改变运行期行为**的修复会污染该基线。
  已在 high-perf-player/status.md 的 Phase 4 条目加交叉备注（沿用 Phase 2 记录"在 test-console-ui 中发现并修复"的先例）。
- **勿误改**：credit 复活路径 `mInFlightFrames == FRAME_QUEUE_MAX_SIZE - 1` 是**边沿触发**，
  本身不会重复拉起链路，**不需要**代次保护，后续勿把 gen 守卫扩到该路径。
- **本修法的内在取舍**：两个唤醒源变成"先到者胜"，500ms 兜底不再是无条件保险。
  当前 `VEDemux`/`VENetworkSource` 的 `putPacket` 必定触发通知，可接受；新增源实现须保证这一点。
- **有界性口径**：链路数不会无界增长（撞 credit 上限时全部退出，边沿触发复活只投一条，每次 park 收敛回 1）。
  持续多条仅发生在 credit 一直不满时，影响是空转耗 CPU，**不是内存泄漏、不是功能错误**。
