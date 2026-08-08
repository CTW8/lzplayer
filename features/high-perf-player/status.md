# high-perf-player 进度

> 最后更新: 2026-08-08
> 总体状态: Doing
>
> 口径说明：六个阶段的代码实施已全部完成且 `./gradlew assembleDebug` 全工程通过，但**均未做真机回归**，因此一律停留在 Doing 段（标注"实现完成 + 构建通过，真机回归未做"），不标 Done。真机回归（见 Todo 第 1 项）是唯一阻塞六个阶段一并转 Done 的事项。

## Done

（无）

## Doing

- [ ] Phase 0.A: 预备 bugfix 批次（6 项全做）— **实现完成 + 构建通过，真机回归未做**
  - SLES configure 成功返回码修正；start 幂等。
  - VEAudioRender 设备硬错误上报 ERROR + `m_DeviceFailed` 闩，修复静默死链。
  - `VEAudioDecoder::onPrepare` 返回值规范化 + 失败清理。
  - 热路径日志降 ALOGV（VEFrame 析构、两个解码器 per-packet 等共 9 处）。
  - GLES shader 色彩量程：新增 `uColorMat`/`uColorOffset`，按 `color_range` 与 colorspace 选 BT.601/709 × full/limited。
  - 解码层坏包容忍：AVERROR_INVALIDDATA 跳包 + 100 连续上限；顺带修正 VEVideoDecoder 误用 `VE_ERROR_EAGAIN`。

- [ ] Phase 0.B: 等价重构 — **实现完成 + 构建通过，真机回归未做**
  - 新增 `interface/IVEComponent.h`、`interface/IMediaDecoder.h`。
  - VEPlayer 改 `std::array<Role, kRoleCount>` 表驱动：9 处命令扇出 + `roleStateFor`/`rolesAllIn`/`setAllRoles`/`anyRoleExists` 全部改遍历，`onFlowTimeout` 的角色判断一并改写。
  - 新增 `core/VESourceRegistry.{h,cpp}`（scheme → 工厂注册表），`createSource` 改查表。
  - 轨道模型：`VEMediaDef.h` 重写为 `VETrackInfo` + `VEMediaInfo` 轨道列表；`codecParams` 深拷贝自持并在析构释放（修悬垂指针）；`IMediaSource::read` 改按 `ETrackType`；`av_find_best_stream` 选默认轨；视频轨解析 `AV_PKT_DATA_DISPLAYMATRIX` 得 `rotationDegrees`，接入 GLES 变换矩阵与 fit-inside 宽高比。
  - 删死代码：VEFrameQueue / VERingBuffer / VEAudioTrack / VEMediaCodecDecoder，以及两个渲染器的残留 dump 代码。
  - ARCHITECTURE.md 全文重写（已完成）。

- [ ] Phase 1: 变速 + 精准 seek 提速 — **实现完成 + 构建通过，真机回归未做**
  - 新增 `core/VESonicProcessor.{h,cpp}`（sonic 包装）。
  - VEAudioRender 双路径：`renderPassthrough` / `renderTimeStretched`，1.0x 完全旁路；pts 记账 `M = P0 + Out/sr × speed`，设备补偿 `× speed`。
  - VEPlayer `kWhatSetSpeed` + `ACTION_SET_SPEED` 串行化；切换即 flush 设备缓冲并 rebase 锚点。
  - VEMediaClock 增 `getPlaybackSpeed`；VEAVsync `getWaitTime` 除以 speed。
  - VEVideoDecoder seek 追赶期 `skip_frame = AVDISCARD_NONREF`（到 target / stop / flush 恢复）。
  - 顺带：音频重采样去双拷贝（`swr_convert` 直写目标帧）；SLES `getQueuedDurationUs` 改按真实样本数累加；Java `setPlaySpeed` 返回值打通。

- [ ] Phase 2: MediaCodec 硬解 + Surface 零拷贝 — **实现完成 + 构建通过，真机回归未做**
  - 新增 `platform/android/decoders/VEMediaCodecVideoDecoder.{h,cpp}`：解码 + 同步 + 上屏合一，Role 表同占 VDEC/VDISPLAY 两槽位、每条命令回两份回执。
  - 新增 `core/VEVideoDecoderFactory.{h,cpp}`：白名单 H264/HEVC + DecoderPolicy。
  - 输入侧 bsf `h264/hevc_mp4toannexb` + csd-0 + `rotation-degrees`；输出侧 `releaseOutputBuffer(render=true)` 零拷贝上屏，丢帧走 `render=false`。
  - seek 用 `AMediaCodec_flush` + 按 pts 丢弃实现精准；`setOutputSurface` 处理 surface 变化。
  - 运行期故障经 `arg2 = VE_INFO_DECODER_FALLBACK` 触发 `VEPlayer::rebuildVideoAsSoftware`（`ACTION_REBUILD_VIDEO` 串行化 + plGen 换代 + seek 回原位续播）。
  - **与原设计的偏差（如实记录）**：改用**同步 API + 自身 looper 工作循环**，而非原计划的 API 28 异步回调——minSdk 24 下异步回调需 dlsym 兜底，收益不抵复杂度；同步 API 自 API 21 可用，因此**原计划"API 24~27 保持软解"的限制取消**，硬解覆盖全部支持机型。
  - **缺陷与修复（2026-08-08，在 test-console-ui 实施中发现并修复）**：`VEPlayer::rebuildVideoAsSoftware` 原本把 `VE_INFO_DECODER_FALLBACK(0x3001)` 直接当**事件号**发 `notifyInfo`，但 JNI 分发与 Java `EventHandler` 都不认识这个号，事件被当未知消息丢弃——**"硬解回退"永远到不了 UI**。已改为走 `ON_INFO` 通道、把 `VE_INFO_DECODER_FALLBACK` 放 `arg1`。真机回归时需专项确认：触发 fallback 后 UI 侧 HUD 由 HW 变 SW 且事件流出现该条目。

- [ ] Phase 3: 网络源 — **实现完成 + 构建通过，真机回归未做**
  - 新增 `core/net/` 目录：`IDataSource.h`；`VEHttpDataSource.{h,cpp}`（socket + OpenSSL TLS、Range、重定向 ≤ 5、连接/读超时、poll 分片使 abort 可打断、SNI）；`VEBufferedDataSource.{h,cpp}`（预取线程 + 32MB 环形缓存 + 起播/低/恢复三水位 + BUFFERING 回调 + 前向 512KB 内等待否则 Range 重定位）；`VENetworkSource.{h,cpp}`（继承 VEDemux 覆写 `openInput` 挂自定义 AVIOContext，节流参数放大到 64MB/10s，abort 双通道）。
  - VEDemux 抽出 protected virtual `openInput` / `maxTotalBytes` / `bufferedDurationTargetUs`；注册表注册 http/https。
  - VEPlayer 处理 BUFFERING_START/UPDATE/END（内部暂停/恢复，不改对外 `mState`；与用户 pause 及 `isFlowBusy` 的并发规则已实现）。
  - CMake 链接 ssl/crypto。
  - **明确不支持**：chunked 编码、HTTP/2、代理。

- [ ] Phase 4: 能耗与调度精修 — **实现完成 + 构建通过，真机回归未做**
  - `IMediaSource` 新增 `requestReadNotify`（one-shot 登记-通知）；VEDemux 实现（per-track 槽 + `putPacket` 触发 + 登记后复查防丢唤醒）；两个解码器饥饿改登记 + 500ms 兜底重试，删除 10ms 轮询。
  - **缺陷与修复（2026-08-08，回答用户 NuPlayer 提问时对照发现）**：上面这条"登记 + 500ms 兜底"**对一次饥饿武装了两个唤醒源**，
    而 `kWhatDecode` 成功后自我续投，两条消息 `epoch` 相同互相过滤不掉 → 形成两条并行自驱解码循环（有界：撞 credit 上限时全部退出，
    每次 park 收敛回 1；credit 一直不满时持续多条空转）。漏抄了 NuPlayer `DecoderBase::onRequestInputBuffers` 的
    `mRequestInputBuffersPending` 去重。**已用 `mStarveGen` 代次修复并验过无回归，独立跟踪于
    [decoder-starve-wake-dedup](../decoder-starve-wake-dedup/)**，其步骤4（正向验证互斥生效）建议与 **Phase 3 网络源真机验证合并做**
    —— 需要持续饥饿的源，`adb reverse` + 限速 HTTP 一次覆盖两件事。
  - 新增 `platform/android/renders/VEAAudioRender.{h,cpp}`（AAudio，LOW_LATENCY，data callback 拉模型 + 内部环形缓冲对接推模型；`getTimestamp` 给真实呈现位置以根治音频时钟精度）。
  - VEAudioRender 按 `isAvailable()` 选后端，configure 失败自动退回 SLES。
  - **重要实现细节**：AAudio 必须用 dlopen + dlsym——直接链接会让 `libaaudio.so` 成为 DT_NEEDED 硬依赖，API 24/25 机型上整个 so 加载失败；已验证产物不含 libaaudio 硬依赖。
  - **未做项**：热路径日志编译期宏裁剪（`VE_LOG_HOT`）未做，日志已在 Phase 0.A 降级为 ALOGV（见 Todo 第 4 项）。

- [ ] Phase 5: 多轨与字幕 — **实现完成 + 构建通过，真机回归未做**
  - 新增 `core/VESubtitleTrack.{h,cpp}`（AHandler + IVEComponent，注册 `ROLE_IDX_SUBTITLE` 槽位）：`avcodec_decode_subtitle2` → cue 队列 → 单定时器按主时钟调度（延时除以 speed）；ASS 剥样式出纯文本；SUBTITLE / SUBTITLE_CLEAR 事件。
  - VEDemux 支持字幕轨：独立小队列 256、不参与 park 判据、`selectTrack`/`onSelectTrack`。
  - VEPlayer 增 `getTrackInfoJson`/`selectTrack`/`deselectTrack`/`addExternalSubtitle`、`ACTION_SELECT_TRACK` 串行化、`switchAudioTrack`（demux 换流 + 异 codec 时重建音频链 + 全链精准 seek 回当前位置，画面定格不黑屏）、`setupSubtitleChain`；外挂字幕独立 ctx 一次性解析成虚拟轨（index ≥ 0x10000）。
  - JNI 新增 4 个方法与注册；Java 新增 `TrackInfo.java`、`VEPlayer.getTrackInfo/selectTrack/deselectTrack/addExternalSubtitle`、`IMediaPlayerListener` 新增 6 个事件常量、NativeLib EventHandler 转发。

## Todo

- [ ] 全部六个阶段的真机回归（lzplayer-test-expert），报告落 `test-reports/` —— **唯一阻塞六个阶段标 Done 的事项**。
- [ ] simpleperf CPU before/after 实测数据（Phase 2 的 1080p30 整机 CPU < 15% 目标尚未验证）。
- [ ] 测试素材准备：多音轨 mkv、内嵌 / 外挂字幕、VP9/AV1（白名单外）素材、限速 HTTP 服务器、长 GOP 1080p、损坏文件。
- [ ] Phase 4 可选项：`VE_LOG_HOT` 编译期日志裁剪、音频解码与渲染合并 looper（原计划即标注为可选）。
- [ ] 代码提交（CLAUDE.md 要求提交前需用户明确同意；当前全部改动未提交）。

## 构建与注意事项

- `./gradlew assembleDebug` 全工程通过，含清 `.cxx` 后的全量重建。
- CMake 新增链接 `mediandk`、`ssl`、`crypto`，新增 `__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` 定义。
- **CMake 用 `file(GLOB_RECURSE)` 收集源文件，增删 `.cpp` 后必须 `touch CMakeLists.txt` 才会重新 configure**，否则新文件不参与编译。
- AAudio 走 dlopen/dlsym，禁止改为直接链接（会引入 DT_NEEDED 硬依赖，低版本机型加载失败）。
