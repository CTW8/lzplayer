# test-console-ui 实施计划

> 设计文档: ../../docs/test-console-ui-design.md
> 创建日期: 2026-08-08

## 方案摘要

把已产出的「LZPlayer 测试台 UI 设计稿」落成实际 Android 界面。定位是**工程测试台**而非消费级播放器，判定标准：high-perf-player Phase 0~5 的每项能力都有可按到的控件 + 当场可读的读数。共四块屏幕：A 主控台（竖屏）、B 轨道面板（BottomSheet）、C 诊断面板（BottomSheet）、D 横屏沉浸。视觉语言为视频测试仪器：SMPTE 彩条分隔、等宽字体读数（tabular nums）、主色去饱和 SMPTE 青 `#3FA9C4` 与语义色 ok/warn/crit 分家、状态不只靠颜色（HW/SW 字样、遮罩、错误标识）。该界面同时是 high-perf-player 真机回归的操作载体。

新增界面代码一律用 Kotlin；与既有 `MainActivity.java` 共存还是替换，实施时决定并回填本文档与 status.md。

## 实施步骤

1. **步骤1 主控台骨架**
   - 落地设计 token：`colors.xml`（主色/语义色/底/面板/发丝线/正文/弱化/三种轨道色）、`themes.xml`、`dimens.xml`（间距/圆角/发丝线宽）、等宽字体资源与 tabular nums 样式；SMPTE 彩条 drawable。
   - 主控台布局（Kotlin Activity + XML）：源栏（本地路径入口 + 网络 URL 输入框，URL 载入逻辑留到步骤7）、SurfaceView 视频区、常驻 HUD（左上解码路径占位、右上状态机状态+速率）、双层进度条 + 时间码、传输控件（播放暂停/停止/循环）、速率分段控件六档、四宫格入口（面板本身可先为空壳）。
   - 接通现有 VEPlayer API：`init/prepare(Async)/start/pause/resume/stop/seekTo/setLooping/setPlaySpeed`，进度与状态由 `IVEPlayerListener` 驱动。
   - 验收：本地文件可完成 选择→prepare→播放→暂停→seek→停止→循环 全链路；六档速率可切且 HUD 速率读数同步；时间码等宽不抖动；四宫格点击有响应（可为空面板）。

2. **步骤2 native 前置接口补齐**（诊断/轨道面板的硬依赖）
   - > **事后更正（2026-08-08 实施）**：下面"已具备…无需重做"的判断**是错的**。`getStats()` / `PlayerStats` / 两个 force setter 当时并不存在，统计数据源（队列深度、缓冲时长、AV 偏移、渲染/丢帧计数、音频后端名）整条链路都需要新建。实际补齐范围见 status.md 步骤2 条目。
   - **~~已具备~~（2026-08-08 核对工作区代码确认，此判断已被实施推翻）**：`VEPlayer.getStats()` → `PlayerStats`（state/decoder/codec/audioBackend/avOffsetMs/renderedFrames/droppedFrames/audioQueue/videoQueue/bufferedMs/source/speed/buffering/positionMs/durationMs）全链路已通到 native `VEPlayer::getStats()`；`setForceSoftwareDecoder(boolean)` / `setForceSlesAudio(boolean)` 两个策略 setter 也已透出 JNI/Java。诊断面板八项读数与两个开关的数据源**齐了**，本步不需重做。
   - **仍缺**：轨道 JSON 字段。`VEPlayer::getTrackInfoJson()` 只输出 `index/type/lang/title/codec/active`，缺**采样率与声道数**（`VETrackInfo` 内部已有 `sampleRate`/`channels`，只是没序列化）；`TrackInfo.java` 相应补字段与解析。codec 名可 Java 侧由 `codecId` 映射兜底。
   - 验收：`getTrackInfo()` 返回的音轨项能读出真实采样率与声道数；`getStats()` 在播放中各字段数值随时间变化合理（非占位）；两个策略 setter 调用后重新 prepare 能观察到解码路径/音频后端切换。

3. **步骤3 轨道面板（BottomSheet）**
   - 接通 `getTrackInfo/selectTrack/deselectTrack/addExternalSubtitle` 全链路。
   - 视频轨只读并灰掉（标注本期不支持切换）；音频轨单选列表（语言/codec/采样率/声道）；字幕轨单选列表含固定「关闭」项；加载外挂字幕按钮。
   - 切轨耗时计时：从发起切换到收到 `ON_TRACK_CHANGED`，在该项原位显示毫秒数。
   - 验收：多音轨文件可切换且声音随之改变；字幕轨可切换与关闭；外挂字幕可加载并出现在列表；硬解下切轨耗时读数可用于对照 < 200ms 验收线。

4. **步骤4 字幕 overlay**
   - 监听 `ON_SUBTITLE` / `ON_SUBTITLE_CLEAR` 渲染文本，带描边保证任意画面上可读。
   - 位置策略：常态贴视频区下部；横屏控件浮出时上移避让（与步骤6 协同，本步先实现上移接口）。
   - 验收：字幕出现/消失时机与音画一致（目视 ±100ms）；暂停/seek/变速下不残留、不错位；控件浮出时不被遮挡。

5. **步骤5 诊断面板（BottomSheet）**
   - 八项读数：解码路径、音频后端、音视频偏移、丢帧/总帧、包队列（音/视）、缓冲水位、源类型、播放器状态；刷新**跟随既有 500ms 进度 tick，不另开定时器**。
   - 事件流列表：事件名与 `VEDef.h` 常量同名（`ON_SEEK_DONE`/`ON_BUFFERING_UPDATE`/`VE_INFO_DECODER_FALLBACK` 等），倒序追加，便于与 logcat 逐条对照。
   - 两个强制开关（强制软解 / 强制 OpenSL ES），UI 上明写"改的是下次建链策略，需重新 prepare 才生效"。
   - 导出日志：事件流 + 读数快照落文件，可 adb pull。
   - 验收：八项读数全部有真实来源无假数据；事件流与 logcat 能逐条对上；两个开关重新 prepare 后 HUD/读数如实反映路径变化；导出文件可取回。

6. **步骤6 横屏沉浸模式**
   - 控件 3 秒无操作自动隐藏、触摸唤出；HUD 常驻不隐藏；缓冲遮罩层。
   - 字幕随控件显隐上移/复位。
   - 验收：旋转前后播放不中断、状态不丢；灰度截图下 HUD 与遮罩仍可判读；色彩量程与旋转角度目视核对通过。

7. **步骤7 网络 URL 与缓冲态 UI**
   - 网络 URL 输入接通 `init` 流程；源类型读数区分 local/http/https。
   - `BUFFERING_START/UPDATE/END` 驱动遮罩显示与百分比数字；双层进度条底层显示已缓冲水位。
   - 验收：网络源可起播；限速/弱网下遮罩与百分比随事件正确出现与消失；缓冲中执行 暂停/seek/停止 不错乱。

8. **步骤8 真机自测与报告**
   - 用 adb-ops 截图逐一核对四块屏幕（A/B/C/D），核对灰度可读性。
   - 逐项对照 Phase 0~5 能力清单，确认"每项能力都有控件、每项状态都有读数"。
   - 报告落 `test-reports/`。
   - 验收：四块屏幕截图齐全；能力覆盖清单无遗漏项；发现的 native 缺陷单独归入 high-perf-player，不在本 feature 混改。

## 依赖与顺序

- 步骤2 是步骤3（轨道字段）与步骤5（读数、开关）的硬前置。
- 步骤4 的横屏避让与步骤6 协同，步骤6 完成后回看一次。
- 步骤7 依赖 high-perf-player Phase 3 的网络源能力已可用。
- 步骤8 依赖前七步全部完成。

## 风险

1. ~~**native 统计接口缺失** → 已消解~~ → **风险属实，已于步骤2 实施中补齐**（2026-08-08）：登记时误判为已具备；实际 `getStats()`/`PlayerStats` 与其数据源（VEDemux 队列深度/缓冲时长、VEAVsync 偏移、渲染器丢帧计数、音频后端名）全部为新增。
2. ~~**策略开关依赖 native setter** → 已消解~~ → **同上，setter 为新增**：`setForceSoftwareDecoder`/`setForceSlesAudio` 及 VEAudioRender `setForceSles` 均在步骤2 实施中补上；UI 已明写"下次 prepare 生效"。
3. **TrackInfo 字段不足**（已于步骤2 补齐 codecName/sampleRate/channels/width/height/rotation）：轨道 JSON 缺采样率/声道，轨道面板信息展示依赖步骤2 补字段；codec 名可 Java 侧由 codecId 映射兜底，采样率/声道必须 native 序列化出来。
4. **Java/Kotlin 混合**：app 模块当前是 Java + 单 `MainActivity`。新增界面代码用 Kotlin；与 `MainActivity.java` **共存还是替换由实施时决定并记录**，倾向先共存（旧界面留作对照），回归通过后再决定是否删旧。
5. **依赖 high-perf-player 尚未真机回归**：本测试台依赖的 native 能力还没在真机验证过，开发过程中可能踩到 native bug。原则：native 缺陷归 high-perf-player 修复，本 feature 只负责暴露与可视化，不混改。
6. **硬解厂商差异**：fallback 必须在 UI 上可见（HUD 的 HW/SW 字样 + 事件流 `VE_INFO_DECODER_FALLBACK`），否则回归时无法判定走的是哪条路径。
7. **诊断刷新开销**：读数刷新严格搭 500ms 进度 tick，禁止另起定时器，避免测试台自身干扰 CPU 占用测量（high-perf-player 的 < 15% 指标）。
