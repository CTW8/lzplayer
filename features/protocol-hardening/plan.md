# protocol-hardening 实施计划

> 设计文档: ../../docs/protocol-hardening-design.md
> 创建日期: 2026-07-23

## 方案摘要

对 lzplayer_core 控制面与数据面按 NuPlayer 设计做根治性重构。控制面：角色状态机 + 状态守卫替代位掩码聚合，notify 模板 pipelineGen 封杀跨管线迟到事件，操作串行化队列（仿 mDeferredActions）保证同一时刻至多一轮长流程，seek 超时改 ERROR 收敛，prepare 异步化并挂 AVIOInterruptCB。数据面：包平面保持拉模型（10ms 延时重试 + 拉取触发补货），帧平面改推 + credit（新接口 IFrameSink，双向防过期：正向 mQueueGen / 反向 mEpoch），音频由 SLES 回调定速驱动全管线，删静音保活；命令态与数据态彻底正交。同步删除 IVEComponent / IMediaDecoder 等旧接口与位掩码全套机制，IMediaSource 瘦身为仅 read()。

## 前置事项

开工前需先处理 deep-review-fixes 步骤 9-13 的未提交改动（待用户同意后提交或另行处置）。

## 实施步骤

每步独立编译验证（`./gradlew assembleDebug` 零警告）、独立 commit；真机回归按用户指示挂起，全部步骤完成后一次补测。

规模排序（大 → 小）：步骤 6 > 5 > 2 > 4 > 3 ≈ 8 > 1 > 7 > 9。

### 1. 接口移除与调用点显式化（暂保位掩码保证可编译）
目标：删 IVEComponent 与虚继承，prepare 全部类型化，下发调用点显式展开。

涉及文件与关键改动：
- 删 `interface/IVEComponent.h`。
- VEDemux 改为 `: AHandler + IMediaSource`；解码器/渲染器直继 AHandler。
- 删两解码器 stub prepare(VEBundle)：`VEAudioDecoder.cpp:496`、`VEVideoDecoder.cpp:43`（含 "surface"→"win" 键名翻译）。
- prepare 类型化：`VEVideoDisplay::prepare(decoder, win, w, h, fps)`、`VEAudioRender::prepare(decoder, VEAudioOutputConfig)`、`VEDemux::prepare(path)`。
- forEachComponent 调用点展开：`VEPlayer.cpp:334`。

验收：编译通过，接口文件删除，调用点无隐式聚合遍历。

### 2. 角色状态机 + pipelineGen + 超时新语义（删位掩码全套）——控制面最大步
目标：回执状态守卫防过期，跨管线迟到事件封杀，seek 超时改 ERROR 收敛。

涉及文件与关键改动：
- `VEPlayer.h`：加 RoleState 枚举 × 5 成员 + mPipelineGen + mFlowSeq。
- mRenderNotifyMsg 等 notify 模板盖 "plGen"，onComponentEvent 首行校验。
- acceptAck 守卫（回执仅在对应 *ING 态被接受）+ finishSeekStageIfPossible / finishTeardownStageIfPossible（全部非 NONE 角色到位后推进）。
- 删 awaitAcks / onComponentAck / activeComponentMask 及 mPendingAcks / mExpectedAckEvent / mAckGeneration / mAckContinuation。
- seek 超时改中止 + ERROR 路径；teardown 超时保留强推 finishTeardown（800ms/段）。

验收：编译通过，位掩码机制不存在，回执仅在 *ING 态被接受。

### 3. 操作串行化队列 + 删换源分支与延时重投
目标：SEEK/RESET/RELEASE/PREPARE 串行化，RELEASE 必回复。

涉及文件与关键改动：
- `PendingAction{type, seekMs, replyToken, wantsReply}` + mPendingActions + mAbortSeek；流程完成出口 processPendingActions。
- isFlowBusy 含 STATE_PREPARING；SEEK 与队尾 SEEK 合并；RESET/RELEASE 入队置 mAbortSeek。
- 删 kDeferWhileReleasingUs 延时重投三处（deep-review-fixes 步骤 9 遗留）。
- 删 onSetDataSource 换源分支：`VEPlayer.cpp:218-222`。

验收：编译通过，并发长流程操作串行化，RELEASE 必回复。

### 4. 包平面改造（10ms 重试 + 拉取触发补货 + 删 needMorePacket 机制）
目标：包平面无跨组件注册回调，demux 数据面防过期与判空。

涉及文件与关键改动：
- 解码器饥饿分支改 postDecode(10ms) 带 epoch：`VEAudioDecoder.cpp:355`、`VEVideoDecoder.cpp:413`；NOT_ENOUGH_DATA 不再置 mIsStarted = false。
- VEDemux read() pop 后低水位（容量/2）且 `!mContinuePending.exchange(true)` 时 post 自家 kWhatContinueRead。
- 删 IMediaSource::needMorePacket 及 VEDemux 的 needMorePacket / onNeedMorePacket / kWhatNeedMore / mNeedAudioMore / mNeedVideoMore / mAudioNotify / mVideoNotify。
- VEDemux 加 atomic mReleased；onRead 首行 mFormatContext 判空。

验收：编译通过，包平面无跨组件注册回调。

### 5. 帧平面推模型·视频链（推 + credit，删视频拉链路）
目标：视频链从拉改推 + credit，双向防过期落地。

涉及文件与关键改动：
- 新建 `interface/IFrameSink.h`：`{ queueFrame(frame, consumedReply); queueGeneration() }`。
- VEVideoDecoder：queueFrame 改推 + consumedReply（带自身 mEpoch）+ mInFlightFrames（上限 6，仅解码 looper 访问，非原子）；kWhatFrameConsumed handler 校验 epoch 还 credit。
- VEVideoDisplay 实现 IFrameSink：atomic mQueueGen；onQueueFrame gen 校验后入裸 std::deque（仅渲染 looper 访问，无锁）；onAVSync 改读本地队列；丢帧/渲染路径均回投回执；onSeekTo / onFlush / onStop 时 ++mQueueGen + 清队列（不为被清帧发回执，解码器 flush 自清 mInFlightFrames = 0）。
- 删 m_pVideoDec 与 readFrame 拉链路；建链顺序反转（先 display 后 decoder）。

验收：编译通过，视频链无跨线程拉调用。

### 6. 帧平面推模型·音频链 + 音频驱动改造——全方案最大步
目标：音频链同改推 + credit，SLES sink 定速，删静音保活；删 IMediaDecoder 收尾。

涉及文件与关键改动：
- VEAudioDecoder：同步骤 5 推模型改造，credit 上限 50。
- VEAudioRender 实现 IFrameSink：onRender 改 while 循环喂 SLES（WOULD_BLOCK 即 break，天然全深度预填，设备队列深度 2）；EOF 帧 → 报 EOS + pause SLES 设备 + 回投回执；onQueueFrame 时队列此前为空且 m_IsStarted 则 postRender(0)（帧到达自然重启）。
- 删静音保活全套、kUnderrunRetryUs、SLES flush 静音预填、m_PendingFrame、mSliceBuffer。
- 删 m_AudioDecoder 越权 pause 解码器：`VEAudioRender.cpp:233`。
- 最后删 `interface/IMediaDecoder.h`；IMediaSource 瘦身为仅 read()（getFileInfo 移回 VEDemux 具体类，VEPlayer 持具体类型直接调）。

验收：编译通过，音频由 sink 回调定速，无静音空转路径。

### 7. ERROR/EOS 收敛 + Driver F2
目标：ERROR/EOS 路径与状态机一致，防超时后覆写 PREPARED。

涉及文件与关键改动：
- `VEPlayer::converge()`：停 tick / pause 时钟 / 显式 stop 组件 / 清 seek 态。
- ERROR 分支：RELEASING 时仅记日志，否则 converge + notifyError。
- COMPLETED 走轻量收敛（显式 pause 组件与时钟）。
- Driver OnPrepared 加 MEDIA_PLAYER_PREPARING 守卫：`VEPlayerDriver.cpp:55-63`。

验收：编译通过，ERROR/EOS 路径与状态机一致。

### 8. 异步 prepare + AVIOInterruptCB
目标：prepare 不阻塞调用线程，卡 IO 可被中断。

涉及文件与关键改动：
- `VEDef.h`：加 VE_NOTIFY_EVENT_PREPARE_DONE = 111。
- VEDemux::prepare 改纯异步 + PREPARE_DONE 回执；atomic mAbortRequest 挂 AVIOInterruptCB；公开 stop()/release() 同步置位 mAbortRequest 后再 post。
- VEPlayer::onPrepare 拆两半，加 STATE_PREPARING 并纳入 Flow 忙判定。

验收：编译通过，prepare 不再阻塞调用线程，卡 IO 可被中断。

### 9. seek 细节收尾
目标：seek 边界行为符合设计。

涉及文件与关键改动：
- `VEMediaClock::resetTo(ptsUs, keepPaused = false)`，暂停态 seek 传 true。
- 无 surface 时 seekStagePrime 不等 FIRST_FRAME 直接完成（补发语义）。

验收：编译通过，暂停态 seek 后保持暂停，无 surface 时 seek 不卡在 prime 阶段。

## 验收标准（整体）

- 每步 `./gradlew assembleDebug` 零警告，独立 commit。
- 真机回归挂起，全部完成后一次补测，重点用例见设计文档：起播预缓冲与首帧、连续拖动进度条、暂停态 seek、seek 片尾、播完重播与循环、坏文件 prepare 中 release、反复 create→play→release 线程与内存、纯音频/纯视频文件、underrun 恢复、转屏与 surface 销毁重建、播完静置功耗。
