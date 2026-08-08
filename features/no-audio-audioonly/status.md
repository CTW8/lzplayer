# no-audio-audioonly 进度

> 最后更新: 2026-08-08
> 总体状态: Done

## 备注

- 本 feature 于 2026-08-08 一次性实施并真机验证通过，四个步骤同日完成。
- 测试素材已在设备 `/sdcard/Movies/`：`noaudio.mp4`、`audioonly.m4a`、`audioonly.mp3`、`shortaudio.mp4`。
- 最有价值的产出是步骤3 的硬解 EOS 早退 bug 修复（详见设计文档第 3 节）。

## Done

- [x] 步骤1: 无音轨播放能力核验 (2026-08-08) — 引擎本来就支持，无需改动；`noaudio.mp4` 真机播放至 EOS 并 complete，硬解 codec 27，音频组件创建计数 0。
- [x] 步骤2: 纯音频权限修复 (2026-08-08) — 根因是 API 33 起权限拆分后 `READ_MEDIA_AUDIO` 从未声明，open 直接 EACCES 被误报成 `demux open failed`(8193)。已加声明 + `ConsoleActivity` 运行期申请；`audioonly.m4a` / `audioonly.mp3` 分别 9.80s / 9.76s 播完 10s 文件，AAudio 后端，零静音帧。
- [x] 步骤3: 修复硬解 EOS 早退 bug (2026-08-08) — `onEOS()` 判据由 `mVideoRender == nullptr`（该成员仅软解赋值，硬解恒 null）改为 `mMediaInfo->hasVideo()/hasAudio()` 轨道存在性；`shortaudio.mp4` 验证音频 EOS 在 2.8s 只记录不判完成，视频播到 9.5s 才 complete（修复前会截掉 6.8s 画面）。
- [x] 步骤4: 真机回归 (2026-08-08) — 四个素材全部通过。

## Doing

（暂无）

## Todo

- [ ] 遗留1: MediaSelector 补齐 AUDIO 类型 — `MediaType` 枚举只有 ALL/VIDEO/IMAGE，`MediaLoader` 只查 image/video，UI 里选不到音频文件（当前只能 `am start --es source` 或手打路径）。需改 `MediaType`、`MediaLoader`、`MediaSelectorActivity` 的 tab 与类型名、`MediaPreviewActivity` 的 8 处 `when` 分支，并为音频设计无画面的预览形态。属共享模块独立 UI 工作，本次未做。
- [ ] 遗留2: 起播约 0.6s 追平 — 无音轨 10s 素材实测 9.36s 播完（`onStart` 起时钟已锚到 0，硬解 configure 还要几十 ms，头几帧迟到后被连续渲染追平；无丢帧日志，确认是追平不是丢帧）。与音频侧起播首秒静音属同类问题，建议合并为一个"起播同步"议题单独跟踪。
