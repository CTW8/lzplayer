# adb 命令全量参考

## 1. 设备管理

| 命令 | 用途 |
|------|------|
| `adb devices -l` | 列出已连接设备（含型号）。`unauthorized` 表示手机端未确认授权弹窗 |
| `adb -s <serial> <cmd>` | 多设备时指定目标设备，serial 来自 `adb devices` |
| `adb kill-server && adb start-server` | 重启 adb 服务（设备识别异常时） |
| `adb tcpip 5555` + `adb connect <ip>:5555` | 切换为 WiFi 连接 |
| `adb reboot` | 重启设备 |
| `adb root` / `adb unroot` | 以 root 重启 adbd（仅 userdebug/eng ROM 有效） |
| `adb wait-for-device` | 阻塞等待设备就绪（脚本中串联用） |

## 2. 日志抓取

| 命令 | 用途 |
|------|------|
| `adb logcat -d -v threadtime > log.txt` | dump 当前全部日志后退出（**默认用这个，不要挂持续模式阻塞会话**） |
| `adb logcat -c` | 清空日志缓冲区（复现问题前先清，减少噪音） |
| `adb logcat -d -s TAG1:V TAG2:D` | 按 TAG+级别过滤（V/D/I/W/E/F） |
| `adb logcat -d --pid=$(adb shell pidof -s <pkg>)` | 只看某个应用进程的日志 |
| `adb logcat -d -v threadtime -t 500` | 只取最近 500 行 |
| `adb logcat -d -b crash` | 只看 crash 缓冲区 |
| `adb logcat -v threadtime > log.txt` (run_in_background) | 持续抓日志，配合复现操作，之后停掉再分析 |
| `adb shell ls /data/tombstones/` + `adb pull` | native 崩溃 tombstone（可能需要 root） |
| `adb bugreport bugreport.zip` | 完整 bugreport（耗时长，最后手段） |
| `adb shell dumpsys media.player` | 系统 MediaPlayer 状态（播放器调试参考） |

过滤技巧：先 dump 全量到文件，再用 Grep 按关键字（`FATAL`、`AndroidRuntime`、`DEBUG`、包名、自有 TAG 如 `VEPlayer`）检索，比 logcat 过滤器更灵活。

## 3. 文件操作

### 公开区域（/sdcard）
| 命令 | 用途 |
|------|------|
| `adb pull /sdcard/<path> <local>` | 拉取文件/目录 |
| `adb push <local> /sdcard/<path>` | 推送文件 |
| `adb shell ls -la /sdcard/Android/data/<pkg>/files/` | 应用外部私有目录（Android 11+ 的 shell 通常仍可读） |
| `adb shell find /sdcard -name "*.mp4" -mmin -10` | 按名称/修改时间找最近生成的文件 |

### 应用内部文件（/data/data/<pkg>，仅 debuggable 包）
| 命令 | 用途 |
|------|------|
| `adb shell run-as <pkg> ls -la files/` | 列出应用内部 files 目录 |
| `adb exec-out run-as <pkg> cat files/<f> > <local>` | 读取单个内部文件（二进制安全用 exec-out） |
| `adb exec-out run-as <pkg> tar -cf - files | tar -xf - -C <localdir>` | 整目录导出 |
| `adb push <local> /data/local/tmp/f && adb shell run-as <pkg> cp /data/local/tmp/f files/` | 写入内部文件（经 /data/local/tmp 中转） |

release（非 debuggable）包 `run-as` 会报 `not debuggable`：改用 debug 包，或设备有 root 时 `adb root` 后直接访问。

## 4. 截图 / 录屏

| 命令 | 用途 |
|------|------|
| `adb exec-out screencap -p > shot.png` | 截图（必须 `exec-out`，`shell` 会因换行转换损坏 PNG）；截完用 Read 工具查看 |
| `adb shell screenrecord --time-limit 10 /sdcard/rec.mp4` + `adb pull` | 录屏（默认上限 180s；SurfaceView 视频画面部分机型录不进去，属正常） |
| `adb shell screenrecord --size 720x1280 --bit-rate 4M ...` | 控制分辨率/码率 |

注意：播放 DRM 内容时截图可能为黑屏。

## 5. 应用控制

| 命令 | 用途 |
|------|------|
| `adb shell am start -n <pkg>/<activity>` | 启动指定 Activity（本项目：`com.example.lzplayer/.MainActivity`） |
| `adb shell cmd package resolve-activity --brief <pkg>` | 查询启动 Activity（不知道入口时先查这个） |
| `adb shell monkey -p <pkg> -c android.intent.category.LAUNCHER 1` | 按 launcher 入口启动（备选） |
| `adb shell am start -a android.intent.action.VIEW -d "file:///sdcard/x.mp4" -t "video/*"` | 隐式 intent（如外部唤起播放） |
| `adb shell am force-stop <pkg>` | 强杀应用 |
| `adb shell pm clear <pkg>` | 清除应用数据（**破坏性，先跟用户确认**） |
| `adb install -r -t app.apk` / `adb uninstall <pkg>` | 安装（-r 覆盖 -t 允许 testOnly）/ 卸载 |
| `adb shell pm list packages | grep <kw>` | 查已安装包名 |
| `adb shell dumpsys package <pkg> | grep version` | 查版本 |
| `adb shell pidof <pkg>` | 查进程 pid（存活判断） |
| `adb shell dumpsys window | grep mCurrentFocus` | 当前前台窗口/Activity |
| `adb shell dumpsys activity top | head -50` | 前台 Activity 详情 |
| `adb shell pm grant <pkg> <permission>` | 授权运行时权限（如 `android.permission.READ_EXTERNAL_STORAGE`，免去点权限弹窗） |
| `adb shell appops set <pkg> MANAGE_EXTERNAL_STORAGE allow` | 授予所有文件访问（Android 11+） |

## 6. UI 交互

| 命令 | 用途 |
|------|------|
| `adb shell input tap <x> <y>` | 点击坐标 |
| `adb shell input swipe <x1> <y1> <x2> <y2> [ms]` | 滑动；ms 拉长（如 1000）可模拟长按拖动（进度条 seek 用） |
| `adb shell input keyevent <code>` | 按键：3=HOME 4=BACK 24/25=音量± 26=电源 66=ENTER 82=MENU 85=播放/暂停 |
| `adb shell input text "hello"` | 输入文本（**不支持中文与空格**，空格用 `%s`；中文需借助 ADBKeyboard 等输入法方案） |
| `adb shell input keyevent --longpress <code>` | 长按按键 |
| `adb shell uiautomator dump /sdcard/ui.xml && adb pull /sdcard/ui.xml` | 导出当前界面控件树 XML |
| `adb shell wm size` / `adb shell wm density` | 屏幕分辨率/密度（换算坐标用） |

### 控件定位方法（不要肉眼猜坐标）
1. `uiautomator dump` 拉取 ui.xml
2. Grep 目标控件的 `text=`、`resource-id=` 或 `content-desc=`
3. 取该节点 `bounds="[x1,y1][x2,y2]"`，中心点 `((x1+x2)/2, (y1+y2)/2)`
4. `input tap` 中心点
5. 再次截图或 dump 验证界面变化

注意：SurfaceView 视频区域在控件树中无内容节点属正常；uiautomator dump 在个别界面会失败（`could not get idle state`），可先 `input keyevent 0` 唤醒或直接依赖截图定位。

## 7. 系统信息

| 命令 | 用途 |
|------|------|
| `adb shell getprop ro.build.version.release` / `ro.build.version.sdk` | Android 版本 / API level |
| `adb shell getprop ro.product.model` | 设备型号 |
| `adb shell getprop ro.product.cpu.abi` | CPU ABI（本项目仅支持 arm64-v8a，装不上先查这个） |
| `adb shell dumpsys meminfo <pkg>` | 应用内存占用 |
| `adb shell top -n 1 | grep <pkg>` | CPU 占用 |
| `adb shell df -h /data` | 存储空间 |
| `adb shell dumpsys battery` | 电池状态 |
| `adb shell settings put system screen_off_timeout 600000` | 调整灭屏超时（长时间调试防锁屏） |
| `adb shell svc power stayon true` | USB 连接时保持亮屏 |
