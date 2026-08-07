# data-model-fixes 进度

> 最后更新: 2026-07-31
> 总体状态: Doing

## Done
- [x] 步骤1: 缺陷1（高）VEFrame::getPts() 返回类型 uint64_t → int64_t，对齐 setPts/timestamp，排查所有调用点无符号语义依赖 (2026-07-30)
- [x] 步骤2: 缺陷2（高）音频重采样 VEFrame 构造后判空，避免 memcpy 空指针解引用 — VEAudioDecoder.cpp:334 构造 audioFrame 后、memcpy 前加判空（getFrame()==nullptr || data[0]==nullptr），失败路径先 av_freep(&out_data) 再 return VE_UNKNOWN_ERROR，对齐视频 convertToYuv420p 防护 (2026-07-30)
- [x] 步骤3: 缺陷3（中）shouldParkRead 增加 2048 包停读门槛 — VEDemux.cpp 在总字节硬停之后、软停之前新增单路包数硬停（音/视频 mXxxPacketQueue->getDataSize()>=kQueueBackstopPackets），kQueueBackstopPackets(2048) 成为"停读等 credit 回流"门槛，putPacket 丢包 ALOGW 哨兵保留 (2026-07-30)
- [x] 步骤4: 缺陷4（低）VEPacket::pts/dts 初始化 — VEPacket.h:73-74 改为 int64_t pts = 0; int64_t dts = 0; 与 VEFrame(timestamp=0,dts=0)、durationUs=0 对齐 (2026-07-30)
- [x] 步骤5: 缺陷6（低）渲染侧 mFrames 深度兜底 — VEAudioRender.cpp 加 constexpr kMaxFramesBackstop=100，VEVideoDisplay.cpp 加 kMaxFramesBackstop=12，onQueueFrame emplace_back 前超阈值则回执最旧帧并 pop_front，阈值取各自 decoder credit(音频50/视频6)的2倍 (2026-07-30)
- [x] 步骤6: 缺陷5（低）音频重采样 linesize[0] 改写坏味道 — 删除 VEAudioDecoder.cpp:349-351 手动改写 linesize[0] 三行（audioFrame->getFrame()->linesize[0] = out_samples_per_channel*...），保留 av_frame_get_buffer 给出的对齐 linesize；调研确认 AudioSLESRender::renderFrame 按 nb_samples*channels*bytes_per_sample 读 data_size、不读 linesize[0]，改写是无效果操作且破坏"linesize 是对齐行距"不变量；非重采样 else 分支原本也未改 linesize，保持一致；不改 SLES 侧、不加 payload-bytes 字段。./gradlew :lzplayer_core:externalNativeBuildDebug BUILD SUCCESSFUL (2026-07-31)

## Doing
- [ ] 步骤7: 整体真机回归（lzplayer-test-expert） — 等待设备就绪后由主对话触发

## Todo
（无）
