# no-audio-audioonly 实施计划

> 设计文档: ../../docs/no-audio-audioonly-design.md
> 创建日期: 2026-08-08

## 方案摘要

支持无音轨（纯视频）与纯音频文件播放。排查后确认：无音轨引擎本来就支持（按 `hasAudio()/hasVideo()` 分别建链，无音频时手动给主时钟起锚），无需改动；纯音频的真正阻塞点是 Android 13+ 的 `READ_MEDIA_AUDIO` 权限从未声明，导致 open 直接 EACCES 被误报成 demux 失败。排查中另发现并修复一个真 bug：`onEOS()` 用 `mVideoRender == nullptr` 判断"无视频链路"，而该成员只在软解分支赋值，硬解恒为 null，导致硬解下音频 EOS 一到就提前结束播放。

## 实施步骤

1. **步骤1 无音轨播放能力核验**
   - 走查 `continuePrepare()` / `onStart()` 的建链与时钟起锚逻辑，确认无音频时不误建音频组件且时钟有基准。
   - 验收：`noaudio.mp4` 真机播放至 EOS 并上报 complete，日志中音频组件创建计数为 0。

2. **步骤2 纯音频权限修复**
   - `app/src/main/AndroidManifest.xml` 增加 `READ_MEDIA_AUDIO`（加注释说明缺失后果）。
   - `ConsoleActivity` 增加 `ensureMediaPermissions()` + `onRequestPermissionsResult()`：API 33+ 申请 `READ_MEDIA_VIDEO` + `READ_MEDIA_AUDIO`，以下走 `READ_EXTERNAL_STORAGE`。
   - 验收：`audioonly.m4a` 与 `audioonly.mp3` 均正常播放到底，AAudio 后端，零静音帧，未误建视频组件。

3. **步骤3 修复硬解 EOS 早退 bug**
   - `VEPlayer::onEOS()` 判据从 `mVideoRender == nullptr` 改为 `mMediaInfo->hasVideo()/hasAudio()` 的轨道存在性判断。
   - 验收：`shortaudio.mp4`（10s 视频 + 3s 音频）在 2.8s 音频 EOS 时不判完成，视频播到 9.5s 才 complete。

4. **步骤4 真机回归**
   - 四个素材各跑一轮，确认起播、推进、EOS、complete 全链路。
   - 验收：四个用例全部通过，无崩溃、无误建组件。

## 实施备注

- 素材已推到设备 `/sdcard/Movies/`：`noaudio.mp4`、`audioonly.m4a`、`audioonly.mp3`、`shortaudio.mp4`，后续回归可直接复用。
- 纯音频文件在 UI 里选不到（MediaSelector 无 AUDIO 类型），测试时用 `am start --es source <path>` 或手打路径。
- **教训**：判断"某条链路是否存在"要看轨道信息，不能拿渲染器成员是否为空代替——硬解与软解组件拓扑不同。

## 改动文件清单

- `lzplayer_core/src/main/cpp/core/VEPlayer.cpp`（`onEOS()` 判据）
- `app/src/main/AndroidManifest.xml`（`READ_MEDIA_AUDIO`）
- `app/src/main/java/com/example/lzplayer/console/ConsoleActivity.kt`（权限申请 + import + `RC_MEDIA_PERM`）
