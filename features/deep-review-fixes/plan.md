# deep-review-fixes 实施计划

> 设计文档: ../../docs/deep-review-fixes-design.md
> 创建日期: 2026-07-23

## 方案摘要

在 NuPlayer 风格重构（8 个优化 commit）基础上，深度 review 发现一批缺陷，按 P0 → P1 → P2 优先级逐项修复：release 链路死锁/abort、音频链路 use-after-free 与唤醒失效、回调断链与 JNI 竞态、空指针与错误路径、编排层生命周期、GLES 画幅与音画同步。

**统一验收标准**：每步完成后 `./gradlew assembleDebug` 全量通过、零新增警告。**不做真机验证**（用户明确要求，开发完为止）。

## 实施步骤

1. **[P0] release 链路必挂修复（两项必须一起修）**
   - 1a. ALooper::LooperThread::requestExitAndWait 持锁 join 死锁（thread/ALooper.cpp:61-67）：置位 mExitPending 后放锁再 join。
   - 1b. JNI 线程退出未 DetachCurrentThread（VEJvmOnLoad.cpp）：在 attach 处注册 pthread_key 析构自动 detach（或 looper 线程退出前 detach）。
   - 验收：assembleDebug 通过，零新增警告。
2. **[P0] OpenSL ES 入队缓冲 use-after-free**（VEAudioSLESRender.cpp:274-285）：renderFrame 把 shared_ptr<VEFrame> 存入 m_FrameQueue，SLES 回调消费掉一个后再弹出。验收：assembleDebug 通过。
3. **[P0] 音频解码器 demux 唤醒链路失效**（VEAudioDecoder.cpp:358 + :119-129）：对齐视频解码器，用 kWhatStart 语义的唤醒消息（带 epoch，避免 mIsStarted 置 false 后被丢弃）。验收：assembleDebug 通过。
4. **[P0] seek 后固定丢 ~20ms 音频 + Enqueue 失败被吞**（VEAudioSLESRender.cpp:277-287）：renderFrame 如实返回入队失败，失败帧不推进时钟，配合 VEAudioRender 延时重试；修正 VEResult 与 SLresult 混比。验收：assembleDebug 通过。
5. **[P1] 回调断链修复**（native_PlayerInterface.cpp notify switch + VEPlayerDriver.cpp:76-80 + IMediaPlayerListener.java）：补齐 ON_COMPLETION/ON_SEEK_DONE 的 JNI 转发、Driver OnEOSListener 转发 ON_EOS、Java 侧补常量，走既有 Native → NativeLib.EventHandler → IVEPlayerListener 链路。验收：assembleDebug 通过。
6. **[P1] Java handle 检查-使用竞态**（NativeLib.java）：所有 native 入口共用同一把锁（对齐 AOSP MediaPlayer），消除 release 并发悬空指针。验收：assembleDebug 通过。
7. **[P1] 空指针防护**：VEAudioDecoder::onStop 判空 mAudioCtx/mFrameQueue（:418,420）；VEVideoDisplay::onPrepare surface 为空时正确处理 m_pVideoDec，使后补 setSurface 可恢复。验收：assembleDebug 通过。
8. **[P1] 错误路径卡死修复**：解码器 do/while(ret != EAGAIN) 对持续错误加退出条件避免无限忙循环；demux av_read_frame 瞬时错误后恢复 mIsStart 状态并上报。验收：assembleDebug 通过。
9. **[P2] STATE_RELEASING 重入**：teardown 进行中的 setDataSource/reset/release 改为排队串行化，不丢上一轮 continuation。验收：assembleDebug 通过。
10. **[P2] 数据竞争与泄漏**：getCurrentPosition/getDuration 与 finishTeardown 的 shared_ptr 数据竞争（用上 mMutex 或改原子访问）；修复 ANativeWindow setSurface 引用泄漏 + null surface 防护。验收：assembleDebug 通过。
11. **[P2] 行为缺口**：播完后 start() 回片头重播；seek 到片尾 FIRST_FRAME 卡 2s 超时修复；mSeekTargetUs 在 onFlush/onStop 清除，避免重播丢帧。验收：assembleDebug 通过。
12. **[P2] GLES 画幅修复**（VEGLESVideoRenderer.cpp:571-577）：去掉 static 顶点数组使画幅可随尺寸变化重算；适配分支改按宽高比比较；清掉构造函数 debug fopen。验收：assembleDebug 通过。
13. **[P2] 音画同步**：音频时钟补偿输出延迟（队列深度+典型输出延迟）；shouldDropFrame 阈值从 500ms 收紧；纯视频文件首次 start 给时钟 resetTo(0) 起锚。验收：assembleDebug 通过。
