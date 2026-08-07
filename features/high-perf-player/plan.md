# high-perf-player 实施计划

> 设计文档: ../../docs/high-perf-player-design.md（详细版，含接口签名/流程/公式/文件清单/验收矩阵）
> 创建日期: 2026-08-07（详细化: 2026-08-07）

## 方案摘要

把 lzplayer_core 升级为高性能、低 CPU 占用播放器：本地文件 + 网络播放 + 变速（0.5x~2.0x 音调不变）+ 精准 seek + 播放中音轨切换 + 字幕显示与切换。基于 2026-08-07 全量架构 review 的结论——现有 NuPlayer 式消息驱动架构健康，按此地基分 6 个阶段演进，不推倒重来。量化目标：硬解后 1080p30 整机 CPU < 15%（simpleperf 实测）、起播/精准 seek < 300ms。每阶段独立可验证，完成后用 lzplayer-test-expert 真机回归，报告落 test-reports/。

各阶段技术细节以设计文档对应章节为准，本文只列步骤与验收要点。

## 实施步骤（按阶段跟踪，6 个阶段）

1. **Phase 0：地基（设计文档 Phase 0 章）**——两个严格分批的子批次：
   - **0.A 预备 bugfix 批次**（行为修正，独立 commit 先行）：SLES configure/start 返回码；音频渲染硬错误上报（死链修复）；VEAudioDecoder::onPrepare 返回值与失败清理；热路径日志降 ALOGV；shader 色彩量程（limited/full range + BT.601/709）；解码层坏包容忍（AVERROR_INVALIDDATA 跳包 + 连续上限）。验收：基线回归 + 三项预期改善逐项确认。
   - **0.B 等价重构**：IVEComponent + Role 表（9 处命令扇出表驱动化）；IMediaDecoder 接口；VESourceRegistry scheme 注册表；轨道模型（VETrackInfo/VEMediaInfo 列表化 + codecParams 自持拷贝修悬垂 + read(ETrackType) 泛化 + av_find_best_stream 选流 + 视频旋转角提取与 GLES 应用）；删 4 个死文件与残留调试代码；刷新 ARCHITECTURE.md。验收：除"多音轨默认选流"与"竖拍视频旋转"两项预期变化外行为完全等价；真机基线全绿后进 Phase 1。

2. **Phase 1：变速 + 精准 seek 提速（设计文档 Phase 1 章）**
   - VESonicProcessor（sonic 包装，插 VEAudioRender 喂设备前，1.0x 旁路）；pts 记账与时钟公式（P0 + Out/sr×speed，设备补偿 ×speed）；SLES getQueuedDurationUs 改按真实样本数。
   - setPlaySpeed 全链路（kWhatSetSpeed + ACTION_SET_SPEED 串行化；切换即 flush + P0 rebase）；VEAVsync getWaitTime 除以 speed。
   - skip_frame = AVDISCARD_NONREF 的 seek 追赶提速（到 target 恢复，stop/flush 兜底）。
   - 顺带：音频重采样 swr_convert 直写目标帧，去双拷贝。
   - 验收：五档变速音调不变/同步不劣化/切换无爆音；变速下 seek/暂停/EOS/循环全回归；长 GOP seek 耗时 before/after。

3. **Phase 2：MediaCodec 硬解 + Surface 零拷贝（设计文档 Phase 2 章）**
   - VEMediaCodecVideoDecoder：解码+同步+上屏合一，Role 表同时占 VDEC/VDISPLAY 两角色位（VEPlayer 状态机零感知）；API ≥ 28 异步模式，24~27 保持软解。
   - 输入：h264/hevc_mp4toannexb bsf + csd-0/1 + rotation-degrees；输出：AVSync 决定 releaseOutputBuffer(render) 时机，丢帧走 render=false。
   - seek 的 AMediaCodec_flush + 按 pts 丢弃实现精准；setOutputSurface 处理 surface 生命周期。
   - VEVideoDecoderFactory：白名单（H264/HEVC）+ 策略开关；创建期 fallback（工厂内改建软解）与运行期 fallback（ACTION_REBUILD_VIDEO 内部重建 + VE_INFO_DECODER_FALLBACK 通知）。
   - 验收：CPU < 15%（simpleperf before/after）；双路径 fallback 注入可靠；与变速叠加回归。

4. **Phase 3：网络源（设计文档 Phase 3 章）**
   - 分层：IDataSource / VEHttpDataSource（socket+openssl、Range、重定向、超时重试、abort 解阻塞）/ VEBufferedDataSource（预取线程 + 32MB 环形缓存 + 三水位 + BUFFERING 事件）/ VENetworkSource（覆写 openInput 挂自定义 AVIOContext，demux 内核零改动复用）。
   - seek：缓存内滑动 / 前向 512KB 内等待 / 其余 Range 重定位。
   - VEPlayer 内部缓冲暂停流程（mBuffering，不改对外状态；与用户 pause、seek、teardown 的并发规则见 3.5）。
   - demux 节流参数网络化可配（64MB/10s）。
   - 验收：四容器格式网络播放；限速下 BUFFERING 事件序列与恢复同步；断网超时上报可 reset；弱网下 release 3s 内有界；缓冲期组合操作无错乱。

5. **Phase 4：能耗与调度精修（设计文档 Phase 4 章）**
   - requestReadNotify one-shot 登记-通知替换 10ms 饥饿轮询（epoch 守卫 + 500ms 保底）。
   - VEAAudioRender（API ≥ 26，LOW_LATENCY，callback 拉模式 + 环形缓冲）；getTimestamp 根治音频时钟精度（删 40ms 常数）；工厂按版本选 AAudio/SLES。
   - VE_LOG_HOT 宏编译期裁剪热路径日志；可选 looper 合并（默认不做，先测后决）。
   - 验收：唤醒次数下降（trace 对比）；AAudio 路径音频正常、同步精度不劣化；simpleperf 模板定稿。

6. **Phase 5：多轨与字幕（设计文档 Phase 5 章）**
   - 轨道 API 全链路：nativeGetTrackInfo(JSON)/nativeSelectTrack/nativeDeselectTrack/nativeAddExternalSubtitle + Driver 状态校验 + Java TrackInfo。
   - 音轨切换（**设计修正**：单 AVFormatContext 下"只动音频链"不成立，改为 demux 换活跃轨 + 全链精准 seek 到 currentMediaTime，画面定格不黑屏，硬解 < 200ms；异 codec 时音频链重建）：ACTION_SELECT_TRACK 串行化，完成报 TRACK_CHANGED。
   - VESubtitleTrack 组件（Role 表 ROLE_SUBTITLE，IVEComponent 命令面全接入）：avcodec_decode_subtitle2 → cue 队列 → 单定时器按主时钟调度（速率折算），SUBTITLE/SUBTITLE_CLEAR 事件；pause/seek/变速交互见 5.3；Java SubtitleView overlay。
   - 字幕轨切换/关闭轻量路径；addExternalSubtitle 独立 ctx 一次性解全量 cue 入虚拟轨（≥0x10000）。
   - 验收：多音轨（同/异 codec）切换、字幕时序（±100ms、暂停/seek/变速/EOS）、外挂加载、四类并发时序（切轨×seek 双向、切轨中 reset、缓冲中切轨）。

## 依赖与顺序

- Phase 0（0.A → 0.B 顺序）是 1/2/3/5 的前置；Phase 1 独立最小先行；Phase 2 是 CPU 目标决定性一步（并改善 Phase 5 切轨定格时长）；Phase 3 改动面最大放后；Phase 4 收尾精修。
- Phase 5 仅依赖 Phase 0，可灵活插排，默认最后（受益于 1/2 的 seek 提速）。
- 每阶段完成后 lzplayer-test-expert 真机回归，报告落 test-reports/。
- 测试素材清单见设计文档末尾表格，开工前备齐。

## 风险

1. MediaCodec 厂商差异 → 双路径 fallback 为验收硬条件；API 28 门槛收窄兼容面。
2. 变速时钟连续性 → 切换即 flush + P0 rebase；回归听爆音。
3. 网络阻塞 IO 与 teardown → interrupt_callback + IDataSource::abort 双通道；release 有界为硬指标。
4. Phase 0 基线纪律：0.A/0.B 严格分批各自回归；0.B 两项预期行为变化（选流、旋转）单独标注。
5. 切轨并发时序经 PendingAction 串行化；"只动音频链"已修正为"全链 seek + 画面定格"。
6. ASS 第一期降级纯文本，API 文档标注。
7. sonic 0.5x 输出膨胀 2 倍，staging/环形缓冲按最大膨胀率预留。
