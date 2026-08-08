# no-audio-audioonly 设计文档

> 创建日期: 2026-08-08
> 关联 feature: [../features/no-audio-audioonly/](../features/no-audio-audioonly/)

## 背景与目标

用户原话："另外需要支持无音轨和纯音频文件的播放"。

即两类此前未验证过的输入：

1. **无音轨文件**（纯视频，如 h264 裸画面的 mp4）
2. **纯音频文件**（无视频轨，如 m4a / mp3）

目标是这两类文件都能正常起播、推进到 EOS 并上报 complete，且不误建不存在轨道对应的组件。

## 排查结论

### 1. 无音轨（纯视频）：引擎本来就支持，无需改动

`VEPlayer::continuePrepare()` 按 `hasAudio()` / `hasVideo()` 分别建链，角色槽位只在对应组件存在时才注册；`onStart()` 中对 `mAudioOutput == nullptr && !mMediaClock->isAnchored()` 的情况会手动 `resetTo(0)` 给主时钟起锚，因此没有音频也有时钟基准。

真机实测 `noaudio.mp4`（640x360 h264，10s，无音轨）：硬解 codec 27，全程未创建任何音频组件（日志计数 0），正常播放至 EOS 并上报 complete。**结论：这一类不需要任何代码改动。**

### 2. 纯音频：真正的阻塞点是 Android 权限，不是引擎

**现象**：`VEDemux::onPrepare couldn't open input` → 上层报 `demux open failed`（错误码 8193），4ms 内失败。

这个现象极易被误判成"解封装不支持纯音频"，从而去 demux 层白翻代码。**它不是解封装问题，是文件根本没打开权限。**

**根因**：Android 13（API 33）起 `READ_EXTERNAL_STORAGE` 对媒体文件失效，拆成 `READ_MEDIA_IMAGES` / `READ_MEDIA_VIDEO` / `READ_MEDIA_AUDIO` 三个分类型权限。本工程 app 模块只声明了 `READ_EXTERNAL_STORAGE`，`MediaSelector` 模块合并进来 `READ_MEDIA_IMAGES` + `READ_MEDIA_VIDEO`，**`READ_MEDIA_AUDIO` 从未被任何模块声明**。于是凡是被 MediaStore 归类为 audio 的文件，open 直接 EACCES。测试机为 Android 16，必然命中。

**修复**：
- `app/src/main/AndroidManifest.xml` 增加 `READ_MEDIA_AUDIO` 声明（已加注释说明缺失后果，避免后人清理时误删）。
- `ConsoleActivity` 新增 `ensureMediaPermissions()` 与 `onRequestPermissionsResult()`：API 33+ 申请 `READ_MEDIA_VIDEO` + `READ_MEDIA_AUDIO`，以下版本走 `READ_EXTERNAL_STORAGE`。

**验证**：`audioonly.m4a`（AAC）与 `audioonly.mp3` 均正常播放，10s 文件分别 9.80s / 9.76s 播完，AAudio 后端，零静音帧，未误建视频组件。

**教训**：`couldn't open input` 这类底层报错要先排除权限/路径，再怀疑格式支持。API 33 的权限拆分对多模块工程尤其容易漏——权限声明分散在各模块 manifest 里，合并结果不看 merged manifest 是看不出缺哪个的。

### 3. 顺带发现并修复一个真 bug：硬解下播放会提前结束

这条是本次排查最有价值的产出。

`VEPlayer::onEOS()` 原本的判据是：

```
videoDone = (mVideoRender == nullptr) || mVideoEOS;
```

问题在于 **`mVideoRender` 只在软解分支被赋值**。硬解时显示端就是解码器自身（MediaCodec 直出 Surface），该成员**恒为 null**。于是 `videoDone` 恒为真——只要音频 EOS 一到就判定整体播放完成，根本不等视频。

**修复**：判据改为轨道存在性，用 `mMediaInfo->hasVideo()` / `hasAudio()` 判断"这条链路是否存在"，而不是看某个渲染器成员是否为空。

**验证用例**（专门构造）：`shortaudio.mp4` = 10s 视频 + 3s 音频。
- 修复前：音频 EOS 在 2.8s 到达即"播完"，截掉后面 6.8s 画面。
- 修复后：2.8s 的音频 EOS 只记录不判完成，视频继续播到 9.5s 才 complete。

**教训（重点）**：**判断"某条链路是否存在"必须看轨道信息，不能拿某个渲染器/组件成员是否为空来代替。硬解与软解的组件拓扑不同**——软解有独立渲染器对象，硬解没有。任何 `xxx == nullptr` 形式的"这条链路不存在"推断，在两种解码路径下都可能是错的。

## 涉及模块与文件

### 修改
- `lzplayer_core/src/main/cpp/core/VEPlayer.cpp` —— `onEOS()` 判据改为轨道存在性
- `app/src/main/AndroidManifest.xml` —— 增加 `READ_MEDIA_AUDIO`
- `app/src/main/java/com/example/lzplayer/console/ConsoleActivity.kt` —— `ensureMediaPermissions()` + `onRequestPermissionsResult()` + import + `RC_MEDIA_PERM`

### 不改动
- demux / 解码 / 时钟逻辑 —— 无音轨与纯音频引擎侧本来就支持

## 测试素材

已推送到设备 `/sdcard/Movies/`，后续回归可直接复用：

| 文件 | 内容 | 用途 |
|------|------|------|
| `noaudio.mp4` | 640x360 h264，10s，无音轨 | 无音轨播放 |
| `audioonly.m4a` | AAC 44.1kHz 单声道，10s | 纯音频（m4a 容器） |
| `audioonly.mp3` | MP3 44.1kHz 单声道，10s | 纯音频（mp3 容器） |
| `shortaudio.mp4` | 10s 视频 + 3s 音频 | 专测 EOS 早退回归 |

## 风险与遗留

1. **MediaSelector 没有 AUDIO 类型**：`MediaType` 枚举只有 ALL / VIDEO / IMAGE，`MediaLoader` 也只查 image/video。因此 UI 里选不到音频文件，只能用 `am start --es source` 或手打路径。补齐需改 `MediaType`、`MediaLoader`、`MediaSelectorActivity` 的 tab 与类型名、`MediaPreviewActivity` 的 8 处 `when` 分支，并为音频设计无画面的预览形态。属共享模块的独立 UI 工作，本次未做。
2. **起播约 0.6s 的追平**：无音轨素材 10s 实测 9.36s 播完。`onStart` 起时钟已锚到 0，而硬解 configure 还要几十 ms，头几帧迟到后被连续渲染追平。无丢帧日志，说明是追平而非丢帧。与音频侧"起播首秒静音"属同类起播收敛问题，建议合并成一个"起播同步"议题单独跟踪。
