# protocol-hardening 设计文档（协议层根治重构）

> 创建日期: 2026-07-23
> 状态: 方案已与用户多轮讨论收敛，尚未开工
> 前置事项: 开工前需先处理 deep-review-fixes 步骤 9-13 的未提交改动

## 背景与目标

对 lzplayer_core 的控制面与数据面按 NuPlayer 设计做根治性重构。把三个隐式假设变成协议保证：

1. **同一时刻至多一轮长流程操作在途**；
2. **过期回执/唤醒不可能污染当前流程**；
3. **组件回执语义严格成立**（PAUSE_DONE 后组件绝不自行复活）。

数据面按"sink 定速、credit 传导、各级自驱"重构，命令面与数据面彻底正交。

## 控制面设计

### 1. 角色状态机替代位掩码聚合
- 每组件一个 RoleState 枚举：NONE / ACTIVE / PAUSING / PAUSED / SEEKING / SEEK_DONE / STOPPING / STOPPED / RELEASING / RELEASED。
- 回执只在对应 *ING 态被接受（状态守卫防过期回执）。
- finishSeekStageIfPossible / finishTeardownStageIfPossible 检查全部非 NONE 角色到位后推进。
- 下发显式展开（仿 NuPlayer flushDecoder 风格）。

### 2. notify 模板 pipelineGen
- VEPlayer 每次建链给组件的 notify 模板打 pipelineGen，onComponentEvent 首行校验，封掉跨管线迟到事件；组件零改动。

### 3. 操作串行化队列（仿 mDeferredActions）
- SEEK / RESET / RELEASE 三种 Flow + PREPARE 纳入忙判定；忙时入队、流程完成出口 processPendingActions。
- SEEK 与队尾 SEEK 合并。
- RESET / RELEASE 入队置 mAbortSeek（当前阶段结束即中止 seek、丢弃队列中 SEEK）。
- RELEASE 的 replyToken 跟 Action 走，保证必回复。

### 4. 超时新语义
- seek 阶段超时不再强推（强推是过期回执制造者），改走 ERROR 收敛。
- teardown 超时保留强推 finishTeardown（release 必须有界，800ms/段）。

### 5. 异步 prepare
- VEDemux::prepare 改异步 + PREPARE_DONE 回执，VEPlayer 增 STATE_PREPARING 纳入 Flow。
- FFmpeg 挂 AVIOInterruptCB（atomic abort，stop/release 置位）解决卡 IO。

### 6. ERROR/EOS 收敛
- 组件 ERROR → 停 tick / 停时钟 / 显式 stop 各组件 / 角色状态归位 / notifyError。
- teardown 期间组件 ERROR 只记日志不改状态。
- seek 各阶段入口检查 mState == STATE_SEEKING && !mAbortSeek。
- COMPLETED 时显式 pause 组件与时钟；删 VEAudioRender 越权 pause 解码器。

### 7. Driver
- OnPrepared 回调仅在 PREPARING 态才置 PREPARED（防超时后覆写）。
- 删 onSetDataSource 换源分支（对齐 AOSP：换源必须先 reset）。

## 数据面设计（sink 定速、credit 传导、各级自驱）

### 1. 包平面（demux → decoder）
- 保持拉模型；解码器饥饿时 postDecode(10ms) 带 epoch 延时重试（仿 NuPlayer DecoderBase 10ms 轮询）。
- demux 补货由拉取触发（read() 检查低水位 → post 自家 kWhatContinueRead，atomic pending 去重）。
- 删除 needMorePacket / onNeedMorePacket / mNotifyMore / mNeedMoreData 全套注册机制。

### 2. 帧平面（decoder → render）
- 从拉改推 + credit（仿 queueBuffer + notifyConsumed）：解码器 post kWhatQueueFrame{frame, generation} 给渲染器，每条帧消息自带消费回执消息。
- 帧队列归渲染器所有；渲染器消费后回投回执还 credit。
- 解码器以在途帧计数为闸（视频 6 / 音频 50），在途 < 上限且命令态 started 才继续解码。
- flush 双侧 generation++ 清算，过期帧/回执丢弃。
- 删除 readFrame / needMoreFrame 跨线程拉链路。

**双向防过期（协议裁定）**：
- 正向：渲染器持 atomic mQueueGen，经 IFrameSink::queueGeneration() 暴露；解码器 post kWhatQueueFrame 时盖章，渲染器 onQueueFrame 校验，不匹配即丢弃。
- 反向：消费回执 kWhatFrameConsumed 由解码器构造并携带自身 mEpoch，渲染器原样回投，解码器收到时校验 epoch。
- 渲染器清队列（seek/flush/stop）时**不为被清帧发回执**；解码器 flush 时自行清算 mInFlightFrames = 0，双侧各自归零，无跨线程对账。

**并发模型（协议裁定）**：
- 帧队列搬到渲染器后用裸 std::deque——仅渲染器 looper 线程访问，无锁。
- 解码器 credit 计数 mInFlightFrames 仅解码 looper 线程访问，无需原子。
- 跨线程共享的只有 atomic mQueueGen（渲染器写、解码器读）。

### 3. 音频驱动
- SLES 回调消费 + 帧到达自然重启（onQueueFrame 时链空闲则踢一脚），删除静音无限保活。
- start 预填设备队列全深度(2)。
- EOS 后无帧到达链自然终结 + 渲染器 pause SLES 设备。

### 4. 命令态/数据态正交
- 命令态仅 VEPlayer 经控制消息改；数据消息（帧到达/帧已消费/续读/延时重试）全带 generation/epoch。
- 组件 atomic mReleased 终态拒绝一切数据面活动。
- VEDemux::onRead 补 mFormatContext 判空。

### 5. 稳态节奏链
音频设备播完缓冲 → SLES 回调 → 渲染器入队下一帧 → 回执还 credit → 解码器解下一帧 → 拉包 → demux 低水位补货。sink 实时消费定速全管线，起播时自然形成预缓冲。

## 接口与删除清单

- 删 IVEComponent 与虚继承 virtual AHandler，组件直继 AHandler；prepare 全部类型化，删两个解码器 stub prepare(VEBundle) 与 "surface"→"win" 键名翻译。
- **新建 interface/IFrameSink.h**：`{ queueFrame(frame, consumedReply); queueGeneration() }`，两个渲染器（VEVideoDisplay / VEAudioRender）实现。承接被删 IMediaDecoder 的抽象职责——拉反转为推，未来 MediaCodec 硬解走同一接口。
- 删 IMediaDecoder（拉接口），抽象点移到帧消息协议（IFrameSink + kWhatQueueFrame/kWhatFrameConsumed）。
- IMediaSource 瘦身为**仅 read()**；getFileInfo 移回 VEDemux 具体类，VEPlayer 持具体类型直接调用。
- 删：forEachComponent / activeComponentMask / awaitAcks / onComponentAck / onAckTimeout 及位掩码成员、deep-review-fixes 步骤 9 的延时重投、换源分支、静音保活。

## 涉及模块

lzplayer_core C++ 引擎：VEPlayer、VEPlayerDriver、VEDemux、VEAudioDecoder、VEVideoDecoder、VEAudioRender（SLES 输出）、VEVideoRender/VEGLESVideoRenderer、时钟、IVEComponent / IMediaDecoder / IMediaSource 接口层。

## 风险与依赖

- 前置：deep-review-fixes 步骤 9-13 的未提交改动需先处理（用户同意后提交或另行处置）。
- 每步独立编译验证、独立 commit；真机回归按用户指示挂起，全部完成后一次补测。
- 帧平面推模型（步骤 5/6）与音频驱动改造为全方案最大风险点，涉及跨线程消息协议重建。

## 决策记录

- 弃位掩码聚合与 seq 贯通（用户决定），改角色状态机 + 状态守卫 + 模板代次。
- 移除 IVEComponent（用户决定）。
- 帧平面抽象用新接口 IFrameSink（queueFrame + queueGeneration）承接 IMediaDecoder 职责；IMediaSource 仅留 read()，getFileInfo 移回 VEDemux 具体类。
- 帧平面双向防过期：正向 mQueueGen（渲染器持有、atomic）、反向 mEpoch（解码器持有、随回执消息往返）；渲染器清队列不发回执，解码器 flush 自清 mInFlightFrames。
- 帧队列裸 std::deque（单 looper 访问无锁）；mInFlightFrames 非原子（仅解码 looper 访问）。
- 帧平面推 + credit、包平面 10ms 轮询（对齐 NuPlayer 实际实现）。
- seek 超时改 ERROR 收敛。
- F3 删换源对齐 AOSP。
- C2 超时 detach 暂缓（AVIOInterruptCB 落地后收益比低）。
- 全局卫生债（VEVideoRender/AudioOpenSLESOutput 死文件、CMake GLOB、Java 调试日志、attached_pic、VEFrame/VEPacket 拷贝语义）另立 feature，不混入本 feature。

## 验收标准

- 每步 `./gradlew assembleDebug` 零警告。
- 真机回归全部完成后一次补测，重点用例：
  - 起播预缓冲与首帧
  - 连续拖动进度条
  - 暂停态 seek
  - seek 片尾
  - 播完重播与循环
  - 坏文件 prepare 中 release
  - 反复 create→play→release 线程与内存
  - 纯音频 / 纯视频文件
  - underrun 恢复（网络/IO 卡顿模拟）
  - 转屏与 surface 销毁重建
  - 播完静置功耗（无静音空转）
