# deep-review-fixes 进度

> 最后更新: 2026-07-23
> 总体状态: Done

## Done
- [x] 步骤 1: [P0] release 链路必挂修复（1a ALooper 持锁 join 死锁 + 1b JNI 线程退出未 DetachCurrentThread） (2026-07-23，commit 01ab965，assembleDebug 通过零新增警告)
- [x] 步骤 2: [P0] OpenSL ES 入队缓冲 use-after-free（VEAudioSLESRender：m_FrameQueue 改在途缓冲队列、回调弹出、flush/stop 先 Clear 再释放） (2026-07-23，commit c8c4a6e，assembleDebug 通过零新增警告)
- [x] 步骤 3: [P0] 音频解码器 demux 唤醒链路失效（VEAudioDecoder：对齐视频解码器，改用 kWhatStart 语义唤醒消息） (2026-07-23，commit ef3d074，assembleDebug 通过零新增警告)
- [x] 步骤 4: [P0] seek 后固定丢 ~20ms 音频 + Enqueue 失败被吞（Enqueue 失败如实上报并留帧重试） (2026-07-23，commit 42227f5，assembleDebug 通过零新增警告)
- [x] 步骤 5: [P1] ON_COMPLETION/ON_SEEK_DONE/ON_EOS 回调断链修复（播放完成/seek完成/EOS 事件补齐到 Java） (2026-07-23，commit 11d04b6，assembleDebug 通过零新增警告)
- [x] 步骤 6: [P1] Java handle 检查-使用竞态（NativeLib.java 所有 native 入口共用对象锁） (2026-07-23，commit 25ad4a3，assembleDebug 通过零新增警告)
- [x] 步骤 7: [P1] 空指针防护（VEAudioDecoder::onStop 判空 / VEVideoDisplay::onPrepare surface 为空路径） (2026-07-23，commit 448001d，assembleDebug 通过零新增警告)
- [x] 步骤 8: [P1] 错误路径卡死修复（解码器忙循环收敛 / demux 读错误分级处理） (2026-07-23，commit 3770db2，assembleDebug 通过零新增警告)
- [x] 步骤 9: [P2] STATE_RELEASING 重入排队串行化 (2026-07-23，commit 597ca81，assembleDebug 通过零新增警告)
- [x] 步骤 10: [P2] shared_ptr 数据竞争 + ANativeWindow 引用泄漏/null 防护 (2026-07-23，commit 597ca81，assembleDebug 通过零新增警告)
- [x] 步骤 11: [P2] 行为缺口（播完重播 / seek 片尾超时 / mSeekTargetUs 清除） (2026-07-23，commit 597ca81，assembleDebug 通过零新增警告)
- [x] 步骤 12: [P2] GLES 画幅修复 + debug fopen 清理 (2026-07-23，commit 76c935e，assembleDebug 通过零新增警告)
- [x] 步骤 13: [P2] 音画同步（时钟输出延迟补偿 / 丢帧阈值 / 纯视频起锚） (2026-07-23，commit 7106175 主体 + 597ca81 VEPlayer 侧起锚，assembleDebug 通过零新增警告)

> 提交说明: 步骤 9-13 改动因 VEPlayer.cpp/VEVideoDisplay.cpp 跨步骤重叠，按互不重叠文件集拆为 3 个 commit：76c935e = 步骤 12（GLES 画幅）；7106175 = 步骤 13 音画同步（不含 VEPlayer 侧）；597ca81 = 步骤 9+10+11 + 步骤 13 的 VEPlayer 侧起锚。

## Doing
（无）

## Todo
（无）

## 遗留事项
- **真机回归测试挂起**：用户指示"不要真机跑，开发完为止"，本 feature 仅做编译级验证（每步 assembleDebug 通过、零新增警告）。真机回归为遗留验证项，后续可走 lzplayer-test-expert 流程补测。
