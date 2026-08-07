# deep-review-fixes 设计文档

> 创建日期: 2026-07-23
> 关联模块: lzplayer_core（C++ 引擎 + JNI + Java 封装）

## 背景与目标

项目已完成一轮 NuPlayer 风格重构（命令-回执-状态机闭环、分阶段 seek、回执握手拆解，共 8 个优化 commit）。在此基础上对 lzplayer_core 做了一轮深度架构 review，发现一批缺陷，涉及 release 链路必挂、音频链路 use-after-free、回调断链、空指针/错误路径、编排层生命周期与音画质量等问题。

本需求目标：按优先级（P0 → P1 → P2）逐项修复上述缺陷。

**约束**：不上真机，只做编译级验证。每个步骤的验收标准统一为 `./gradlew assembleDebug` 全量通过、零新增警告。

## 缺陷清单与修复思路

### P0 - release 链路必挂

1. **ALooper::LooperThread::requestExitAndWait 持锁 join 死锁**（thread/ALooper.cpp:61-67）
   - 现象：requestExitAndWait 持 mLock 期间 join，而 threadLoop 每轮要抢同一把锁才能退出，互相等死。
   - 修法：置位 mExitPending 后放锁再 join。
2. **JNI 线程退出未 DetachCurrentThread**（VEJvmOnLoad.cpp）
   - 现象：attach 过 JVM 的 ALooper 线程退出时 ART 会 FATAL abort。
   - 修法：在 attach 处注册 pthread_key 析构自动 detach（或 looper 线程退出前 detach）。
   - 与缺陷 1 必须一起修，否则 release 链路仍必挂。

### P0 - 音频链路

3. **OpenSL ES 入队缓冲 use-after-free**（VEAudioSLESRender.cpp:274-285）
   - 现象：Enqueue 的 PCM 指针来自入队后立即析构的 VEFrame。
   - 修法：renderFrame 把 shared_ptr<VEFrame> 存入 m_FrameQueue，SLES 回调消费掉一个后再弹出。
4. **音频解码器 demux 唤醒链路失效**（VEAudioDecoder.cpp:358 + :119-129）
   - 现象：needMorePacket 的唤醒消息没带 epoch，且发起时 mIsStarted 已被置 false，唤醒必被丢弃。
   - 修法：对齐视频解码器，用 kWhatStart 语义的唤醒消息。
5. **seek 后固定丢 ~20ms 音频 + Enqueue 失败被吞**（VEAudioSLESRender.cpp:277-287）
   - 现象：flush 预填满队列导致 seek 后首帧 Enqueue 必失败且被吞掉、时钟被没播的帧推进；VEResult 与 SLresult 混比。
   - 修法：renderFrame 如实返回入队失败，失败帧不推进时钟，配合 VEAudioRender 的延时重试。

### P1 - 回调断链与 JNI 崩溃

6. **ON_COMPLETION/ON_SEEK_DONE/ON_EOS 事件断链**（native_PlayerInterface.cpp notify switch + VEPlayerDriver.cpp:76-80 + IMediaPlayerListener.java 缺常量）
   - 现象：ON_COMPLETION/ON_SEEK_DONE 在 JNI notify() 被 default 丢弃；Driver OnEOSListener 不转发 ON_EOS。
   - 修法：补齐事件转发链路到 Java（保持 Native → NativeLib.EventHandler → IVEPlayerListener 链路）。
7. **Java handle 检查-使用竞态**（NativeLib.java）
   - 现象：除 release 外所有方法无锁两步访问 mHandle，release 并发时悬空指针。
   - 修法：所有 native 入口共用同一把锁（对齐 AOSP MediaPlayer）。

### P1 - 空指针与错误路径

8. **空指针防护**：VEAudioDecoder::onStop 不判空 mAudioCtx/mFrameQueue（:418,420）；VEVideoDisplay::onPrepare surface 为空时 m_pVideoDec 未赋值且后补 setSurface 无法恢复。
9. **错误路径卡死**：解码器 do/while(ret != EAGAIN) 对持续错误无限忙循环；demux av_read_frame 瞬时错误后 mIsStart 卡 true 永久停摆且无上报。

### P2 - 编排层与生命周期

10. **STATE_RELEASING 重入**：teardown 进行中再来 setDataSource/reset/release 会重启拆解、丢上一轮 continuation。改为排队串行化。
11. **数据竞争与泄漏**：getCurrentPosition/getDuration 与 finishTeardown 的 shared_ptr 数据竞争（用上从未使用的 mMutex 或改原子访问）；ANativeWindow 每次 setSurface 泄漏引用 + null surface 无防护。
12. **行为缺口**：播完后 start() 应回片头重播；seek 到片尾 FIRST_FRAME 永不到达卡 2s 超时 + mSeekTargetUs 在 onFlush/onStop 不清除导致重播丢帧。

### P2 - 音画质量

13. **GLES 画幅与调试残留**（VEGLESVideoRenderer.cpp:571-577）：static 顶点数组导致画幅只算一次；适配分支按帧方向而非宽高比比较；顺带清掉构造函数里的 debug fopen。
14. **音画同步**：音频时钟补偿输出延迟（队列深度+典型输出延迟）；shouldDropFrame 阈值从 500ms 收紧；纯视频文件首次 start 给时钟 resetTo(0) 起锚。

## 涉及模块

- `lzplayer_core` C++ 层：thread/ALooper.cpp、VEJvmOnLoad.cpp、VEAudioSLESRender.cpp、VEAudioDecoder.cpp、VEAudioRender、VEVideoDisplay、VEGLESVideoRenderer.cpp、VEPlayerDriver.cpp、demux、时钟/同步模块
- `lzplayer_core` JNI 层：native_PlayerInterface.cpp
- `lzplayer_core` Java 层：NativeLib.java、IMediaPlayerListener.java

## 风险与依赖

- **不做真机验证**（用户明确要求），行为正确性依赖代码审查与编译通过；后续如需回归，另行走 lzplayer-test-expert 流程。
- 缺陷 1 与缺陷 2 必须捆绑修复（同一步骤），单修任一个 release 链路仍会挂。
- 音频链路三项（缺陷 3/4/5）相互关联（帧队列、唤醒、时钟推进），修复顺序按 3 → 4 → 5 进行以减少交叉影响。
- Native 改动后需完整重新构建对应模块（CMake 侧改动 Gradle 不一定感知）。
