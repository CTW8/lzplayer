---
name: adb-ops
description: Android 设备 adb 操作技能，具备 adb 全量命令知识与操作能力。当需要操作连接的 Android 设备/模拟器时使用：抓取 logcat 日志、定位崩溃、拉取应用内部文件或手机公开文件、截图/录屏、安装/启动/停止应用、模拟点击/滑动/按键输入、导出界面控件树、在真机上验证功能（如播放、seek、暂停）。触发词示例："抓下日志"、"截个图看看"、"启动 app"、"点一下播放按钮"、"把手机上的 xx 文件拉下来"、"在真机上验证"。
allowed-tools: Bash, Read, Write, Grep, Glob
---

# adb-ops：Android 设备操作

通过 adb 对连接的 Android 设备执行日志抓取、文件读写、截图、应用控制与 UI 交互。

**全量命令参考**: [references/commands.md](references/commands.md) — 遇到本文件未覆盖的操作先查它，不要凭记忆拼命令。

## 前置检查（每次会话首次操作前执行一次）

```bash
adb devices -l
```
- 无设备 → 提示用户连接/授权；`unauthorized` → 提示手机上点确认。
- 多台设备 → 问用户用哪台，之后所有命令带 `-s <serial>`。

本项目默认包名 `com.example.lzplayer`，入口 `com.example.lzplayer/.MainActivity`（不确定时用 `cmd package resolve-activity --brief` 查证）。

## 核心工作流

### 抓日志定位问题
```bash
adb logcat -c                                   # 1. 清缓冲
# 2. 复现操作（启动 app / 点击 / 播放...）
adb logcat -d -v threadtime > <scratchpad>/log.txt   # 3. dump 到文件
```
然后用 Grep 检索 `FATAL|AndroidRuntime|DEBUG|libc|VEPlayer|<包名>`。
- 默认用 `-d` dump 模式；确需持续抓取时用 run_in_background，事后停掉再分析。
- native 崩溃：先查 `-b crash` 缓冲区，再看 `/data/tombstones/`。

### 拉取文件
- 公开文件：`adb pull /sdcard/<path> <scratchpad>/`
- 应用内部文件（debug 包）：`adb exec-out run-as <pkg> cat files/<f> > <scratchpad>/<f>`
- release 包 `run-as` 会失败，如实告知限制，建议换 debug 包。

### 截图查看
```bash
adb exec-out screencap -p > <scratchpad>/shot.png
```
必须 `exec-out`（`shell` 重定向会损坏 PNG）。截完立即用 Read 查看图片再下结论。

### 启动/控制应用
```bash
adb shell am start -n com.example.lzplayer/.MainActivity
adb shell am force-stop com.example.lzplayer
adb shell dumpsys window | grep mCurrentFocus    # 确认前台 Activity
```
`pm clear`（清数据）是破坏性操作，执行前先向用户确认。

### 点击/操作界面（截图 → 定位 → 点击 → 验证）
1. 截图了解当前界面
2. `adb shell uiautomator dump /sdcard/ui.xml && adb pull /sdcard/ui.xml <scratchpad>/`
3. Grep 控件的 `text=`/`resource-id=`，取 `bounds="[x1,y1][x2,y2]"` 算中心点
4. `adb shell input tap <cx> <cy>`
5. 再截图/再 dump 验证操作生效
- **禁止肉眼估坐标**；SurfaceView 视频区在控件树无子节点属正常。
- 滑动/seek：`input swipe x1 y1 x2 y2 <ms>`；返回/HOME：`input keyevent 4` / `3`。
- `input text` 不支持中文和空格（空格用 `%s`）。

## 约定

- 所有落地文件（日志、截图、ui.xml、拉取的文件）一律放 scratchpad 目录，不污染项目目录。
- 不执行阻塞会话的持续命令（裸 `adb logcat`、`adb shell` 交互模式）。
- 破坏性操作（`pm clear`、`uninstall`、`reboot`、删除设备文件）先向用户确认。
- 操作后要有验证：启动后确认前台 Activity，点击后确认界面变化，不要只报"命令已执行"。
