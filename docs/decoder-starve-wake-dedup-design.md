# decoder-starve-wake-dedup 设计文档

> 创建日期: 2026-08-08
> 缺陷发现场景: 回答用户关于 NuPlayer `DecoderBase::onRequestInputBuffers` 去重机制的提问时顺带对照发现
> 缺陷引入来源: high-perf-player **Phase 4**（`requestReadNotify` one-shot 通知替换 10ms 饥饿轮询那一批改动）
> 相关但非归属: demux-nuplayer-refactor（同一条数据面链路的接口解耦，但其范围明确是"不改驱动模型"）

## 归属判断（为什么单列而不并入 demux-nuplayer-refactor）

用户倾向并入 `demux-nuplayer-refactor`，本文档给出不并入的理由，请复核：

1. **引入者不是它**。双唤醒源（`requestReadNotify` + 500ms 兜底）是 high-perf-player Phase 4 引入的；
   demux-nuplayer-refactor 只做 `VEPlayer`↔`VEDemux` 的接口边界解耦，其 plan 第 9 行写明
   "驱动模型内核（读循环自驱、消费侧拉取唤醒、命令面消息回执）**行级保持不变**"。
2. **会污染它的验收基线**。该 feature 只剩步骤5 真机回归，验收标准正是"确认驱动模型内核行为无回归"。
   把一处**故意改变运行期行为**（少跑一条解码链路）的修复塞进去，会让那次回归无法回答
   "回归是重构引起的还是这个修复引起的"。
3. **本缺陷的关键验收项独立且尚未完成**，需要专门造饥饿场景（见步骤4）。挂在两个
   大 feature（high-perf-player 6 阶段待回归 / demux-nuplayer-refactor 待回归）下都会被埋掉。
4. 修复本身很小（两个解码器各加一个 `int32_t` 成员 + 三处 gen 读写），单列后一步收尾即可 Done。

作为折中，**在 high-perf-player/status.md 的 Phase 4 条目下加交叉备注**指向本 feature，
沿用 Phase 2 记录"在 test-console-ui 实施中发现并修复"的既有先例。

## 一、缺陷

`VEVideoDecoder::onDecode` / `VEAudioDecoder::onDecode` 从上游读包拿到 `VE_NOT_ENOUGH_DATA`（饥饿）时，
**对同一次饥饿同时武装了两个唤醒源**：

- `mDemux->requestReadNotify(track, notify)` —— 一次性登记，数据入队（`putPacket`）时唤醒；
- `backstop->post(kStarveBackstopUs)` —— 兜底重试，`kStarveBackstopUs = 500000`（500ms），
  设计意图是"防源实现漏发通知"。

而 `kWhatDecode` 分支在 `onDecode()` 返回 `VE_OK` 后会 `postDecode()` **自我续投** —— 解码循环是自驱的。
于是常见情况下两者都会触发：通知在几十毫秒后拉起一条自驱链路开始正常解码；兜底消息在 500ms 时到达，
`epoch` 与当前一致（中间没有 flush/seek），于是被放行，拉起**第二条**并行自驱解码循环。
之后每次饥饿都可能再叠加一条。

### 有界性（准确口径，不是无界泄漏）

链路数**不会无界增长**：

- 稳态下解码器会频繁撞 credit 上限，此时每条链路的 `onDecode` 都拿到 `VE_NO_MEMORY` 而**全部退出**；
- 随后 credit 复活是**边沿触发**（`mInFlightFrames == FRAME_QUEUE_MAX_SIZE - 1` 时才投一条），
  只会拉起**一条**链路；
- 因此每次 credit park 都把链路数**收敛回 1**。

**真正会持续多条的场景**：credit 一直不满（渲染消费速度跟得上解码，解码器不撞上限），
此时多余链路无处收敛，持续空转，每条都在调 `onDecode`（做无谓的 `receive_frame`/`read` 尝试）。
影响是 CPU 与 looper 消息量的浪费，非功能性错误、非内存泄漏。

### 与 NuPlayer 的对照

- NuPlayer `DecoderBase::onRequestInputBuffers()` 用 `mRequestInputBuffersPending` 布尔量做去重，
  且它是**纯 10ms 轮询**，没有"数据到了叫我"的通道；
- 本项目是**事件驱动 + 500ms 兜底**，能力上是 NuPlayer 的超集（唤醒更快、空转更少），
  但**漏抄了它那层去重**——NuPlayer 只有一个唤醒源都要去重，我们有两个反而没有。

### 一处正确的对照（无需保护，勿误改）

credit 复活路径 `mInFlightFrames == FRAME_QUEUE_MAX_SIZE - 1` 是**边沿触发**，本身不会重复拉起链路，
**不需要**这层代次保护。后续维护勿把 gen 守卫扩到该路径上。

## 二、修法

用**代次（generation）让同一次饥饿的两个唤醒源互斥**，与代码里既有的 `epoch` / `queueGen` 同一套路，
不引入新的状态机风格：

- `VEVideoDecoder` / `VEAudioDecoder` 各新增成员 `int32_t mStarveGen = 0;`
- 饥饿分支：`const int32_t starveGen = ++mStarveGen;`，**通知消息与兜底消息都 `setInt32("starveGen", starveGen)`**
  （两者原有的 `setInt32("epoch", mEpoch)` 保留不动）；
- `kWhatDecode` 处理里，在 `epoch` 校验与 `mIsStarted` 校验**之后**插入：
  - 若消息带 `starveGen` → 校验；`!=` 当前值直接丢弃（`ALOGV stale starve wake`）；
  - `==` 则接受，并 `++mStarveGen` 让**兄弟消息**（另一个唤醒源）作废；
- **不带 `starveGen` 的消息一律放行**：自驱续投 `postDecode()`、credit 复活、首次 `start`。
  这条是关键——守卫只作用于饥饿唤醒，不改动正常解码循环的任何行为。

三处校验的顺序语义：`epoch`（flush/seek 换代）→ `mIsStarted`（命令态）→ `starveGen`（唤醒源去重），
彼此正交，命令态与数据面的分离保持不变。

## 三、涉及模块

| 文件 | 改动 |
|------|------|
| `lzplayer_core/src/main/cpp/core/VEVideoDecoder.h` | 新增成员 `mStarveGen`（约 137 行） |
| `lzplayer_core/src/main/cpp/core/VEVideoDecoder.cpp` | `kWhatDecode` gen 校验（约 148~157 行）；饥饿分支双消息带 gen（约 480~488 行） |
| `lzplayer_core/src/main/cpp/core/VEAudioDecoder.h` | 新增成员 `mStarveGen`（约 133 行） |
| `lzplayer_core/src/main/cpp/core/VEAudioDecoder.cpp` | 同构改动（约 138~143 行 / 449~457 行） |

`VEDemux::requestReadNotify`、`IMediaSource` 接口、credit 记账、渲染侧一律**未改**。

## 四、验证状态（有一半未验到，如实记录）

### 已验证：无回归

三种素材均播至 COMPLETED，与修改前**逐项一致**：

| 素材 | 渲染帧数 | 丢帧 | 时长 |
|------|----------|------|------|
| 1080p 硬解 | 278（与改前完全一致） | 0 | 9529ms |
| 1080p 软解 | 287（与改前完全一致） | 0 | 9529ms |
| 纯音频 m4a | 0（无视频轨，预期） | 0 | 10000ms |

### 未验证：「两个唤醒源互斥」这条路径本身

本地文件几乎不饥饿：

- 1080p 两次实测**饥饿次数 = 0**（demux 预填足够，读循环跑在解码前面）→ 守卫代码根本没被执行；
- 纯音频那次**饥饿 1 次**，但"被作废的兜底唤醒"计数为 **0**。
  推断：饥饿发生在流尾，解码器随后 EOS 停止；500ms 后兜底消息到达时先被 `!mIsStarted` 拦下，
  **走不到代次校验**（校验顺序在 `mIsStarted` 之后）。

### 已有的间接证据（只能证伪"误杀"，不能证明"生效"）

纯音频那次真实饥饿之后仍播到 COMPLETED —— 若守卫把 `requestReadNotify` 的正常通知误判为过期而丢弃，
该文件会卡死在饥饿点。故可判定**守卫没有误杀正常唤醒**；但"多余链路确实被消掉了"仍无正面证据。

## 五、待验收项（步骤4）与可行路径

需要一个**会持续饥饿**的源。两条路径：

- **(a) 接 HTTP 源（推荐）**：`adb reverse` 把本机文件用简易 HTTP 服务暴露给设备，
  必要时限速逼出持续饥饿。附带收益：**high-perf-player Phase 3 的网络源链路
  （`VEHttpDataSource`/`VEBufferedDataSource`/`VENetworkSource`）至今没有任何真机验证**，
  这一步可顺带覆盖。
- **(b) 临时调小 demux 缓冲水位**逼它饥饿。更快，但需改测试用代码（`maxTotalBytes` /
  `bufferedDurationTargetUs`），改完必须回滚，且改的是被测系统本身。

**选 (a)。**

### 观测手段的硬约束

当前代码里**没有任何持久化的饥饿/去重计数器**（`grep -rn starve` 只命中两个解码器的 ALOGV）。
而测试机 OPPO PHK110 单进程 **logcat 有 300 行配额**，ALOGV 会把计数日志一起冲掉，
按 `features/status.md` 环境备注，**验证不得只依赖 logcat**。因此步骤4 需先加一对可读回的计数：
`starveCount` / `staleStarveWakeCount`（每个解码器各一对），写文件后
`adb shell run-as com.example.lzplayer cat files/<path>` 读回；
或挂到 perf-metrics 的性能面板上（与 perf-metrics 步骤5 的六分页协商落位，避免各自造脚手架）。

**判定标准**：持续饥饿场景下 `starveCount > 0` 且 `staleStarveWakeCount > 0`
（证明兄弟消息确实被作废）、播放能到 COMPLETED、丢帧不增加。

## 六、风险与依赖

- **风险（低）**：若未来有某个源实现**既不发通知、又让 500ms 兜底成为唯一唤醒**，
  代次逻辑本身不影响它（单一唤醒源时 gen 必然匹配）；但若某源**先发通知后又漏送数据**，
  被作废的兜底就不再兜底了 —— 这是本修法内在的取舍：**两个唤醒源变成"先到者胜"，
  兜底不再是无条件保险**。当前 `VEDemux`/`VENetworkSource` 的 `putPacket` 一定触发通知，可接受。
- **依赖**：步骤4 依赖 (a) HTTP 源可用 —— 而网络源自身未经真机验证，
  若步骤4 暴露的是网络源的问题，应把该问题回填到 high-perf-player Phase 3，不在本 feature 里修。
- **与 vulkan-renderer / startup-cpu-opt 的文件冲突**：无，本 feature 只碰两个解码器文件。
