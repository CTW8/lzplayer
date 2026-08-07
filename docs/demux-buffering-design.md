# demux-buffering 设计文档（demux 缓冲策略重构）

> 创建日期: 2026-07-24
> 关联前序: protocol-hardening（本 feature 是其 review 的后续修正）

## 背景与目标（为什么）

protocol-hardening 完成后，对 `VEDemux` 做深度 review，发现一个架构级缺陷：

- `VEDemux::onRead()` 的 park 条件是「**任一队列满就停整个读循环**」。
- 但 `av_read_frame` 是**单一交织读取器**：音视频包混着出，无法只读某一路。

由此产生**队头阻塞（head-of-line blocking）**，软解高分辨率时必现：

1. 视频软解慢 → 视频队列填满 100 个包。
2. 读循环 park（因为「任一队列满」）。
3. 音频队列被逐渐抽干（消费端仍在取音频包）。
4. `scheduleContinueReadIfNeeded` 唤醒后，`onRead` 发现视频仍满 → 立刻又 park。
5. 音频 underrun → **断音**。

附带问题：队列上限按 **packet 数**（各 100）而非字节/时长，导致：
- 无内存上界（高码率下内存不可控）。
- 两路缓冲的**时长不对等**（同样 100 个包，音频和视频覆盖的时长差异很大）。

## 修正方案（是什么 / 怎么做）

核心思路：去掉「单路满就停」，改为 **ffplay 式**策略——
**全局字节封顶（唯一硬停，防 OOM） + 每路各自缓冲够了才停**。
任何单路都不再单独触发 park。

### 节流策略伪码

```
streamHasEnough(queue, exists):
    if (!exists) return true                       // 不存在的流视为已满足
    if (queue.getDataSize() < kMinPackets) return false
    durUs = queue.getDurationUs()
    return durUs <= 0                              // 容器无 duration 信息时只看包数
           || durUs >= kBufferedDurationTargetUs

shouldParkRead():
    if (audioBytes + videoBytes >= kMaxTotalBytes)
        return true                               // 唯一硬停：防 OOM
    return streamHasEnough(audio) && streamHasEnough(video)
```

### 常量（对齐 ffplay）

| 常量 | 取值 | 含义 |
|------|------|------|
| `kMaxTotalBytes` | `16 * 1024 * 1024` | 全局字节封顶（唯一硬停） |
| `kBufferedDurationTargetUs` | `1000000`（1s） | 每路目标缓冲时长 |
| `kMinPackets` | `25` | 每路最小包数下限 |

## 涉及模块 / 文件

- `VEPacket.h`：新增 `int64_t mDurationUs` + set/get。
- `VEPacketQueue.h` / `.cpp`：新增 `mTotalBytes` / `mTotalDurationUs` 记账；getter `getTotalBytes()` / `getDurationUs()`；`mMaxSize` 抬到 ~2048（降级为防失控兜底）。
- `VEDemux.cpp`：`onRead` 计账与 park 判据、`scheduleContinueReadIfNeeded` 唤醒判据、新增 `shouldParkRead()` / `streamHasEnough()` helper + 常量；及一批捆绑的低风险修正。

## 风险与依赖

- **核心风险**：byte / duration 累计量的加减**必须严格锁在队列类内**（`put` 加、`get` 减、`clear` 归零，全在已有 mutex 内），否则计数器漂移导致 park 判据失真。
- 真机回归按用户指示**挂起**，本轮只做编译验证。

## 明确不含（归入将来的「卫生债」feature）

- `getFileInfo` 缓存。
- `VEPacket` 拷贝构造 double-free。

## 验收标准

- 每步 `./gradlew assembleDebug` 零警告。
- 真机回归挂起（用户指示不上真机）。完成后补测重点：
  1. 软解 1080p + 音频是否还断。
  2. 4K / 高码率内存峰值守在 ~16MB。
  3. 起播预缓冲时延。
