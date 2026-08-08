# Repository Guidelines

## 项目结构与模块组织
- `app/`：主应用与 UI 入口，核心逻辑在 `app/src/main/java/com/example/lzplayer/MainActivity.java`，布局与资源在 `app/src/main/res/`。
- `lzplayer_core/`：播放器核心 JNI 层，`VEPlayer`/`NativeLib` 负责与 C++ 引擎交互；原生代码在 `lzplayer_core/src/main/cpp/`（包含解码、渲染、线程与工具子目录）。
- `MediaSelector/`：媒体选择器库，入口 `MediaSelectorActivity`，负责权限检查与 MediaStore 加载。
- `MediaPipeline/`：媒体处理管线库（目前以 native 构建/依赖为主）。
- `VERecorder/`：录制模块（包含 native 构建）。
- 测试目录统一为各模块的 `src/test/java`（单测）与 `src/androidTest/java`（仪器测试）。

## 架构与核心流程
- 选择流程：`MainActivity` 启动 `MediaSelectorActivity` → 读取 MediaStore → 返回 `selected_files` 路径。
- 播放流程：`MainActivity` 创建 `VEPlayer` 并 `registerListener` → `init(path)` → `setSurface(SurfaceView)` → `prepareAsync()`。
- 回调链路：Native 层通过 `NativeLib.EventHandler` 派发 `onPrepared/onProgress/onEOS` 到 `IVEPlayerListener`，驱动 UI/进度条更新与状态切换。
- 渲染与音频：原生层链接 FFmpeg、OpenSLES、GLES/EGL、libyuv/sonic（见 `lzplayer_core/src/main/cpp/CMakeLists.txt`）。

## lzplayer_core 内部架构与流程（C++/JNI）
- JNI 入口：`VEJvmOnLoad.cpp` 注册 `NativeLib` 方法表；`native_PlayerInterface.cpp` 将 Java 调用转到 `VEPlayerDriver`。
- 驱动层：`VEPlayerDriver` 在 `player_thread` 的 `ALooper` 中运行 `VEPlayer`，维护状态机并触发回调。
- 消息驱动：`VEPlayer` 继承 `AHandler`，通过 `AMessage` 分发 `setDataSource/prepare/start/seek/pause/stop` 等事件。
- 媒体管线：`VEDemux` 基于 FFmpeg 读包 → `VEAudioDecoder`/`VEVideoDecoder` 解码 → `VEPacketQueue`/`VEFrameQueue` 传递 → `VEAudioRender`/`VEVideoDisplay` 渲染，`VEAVsync` 负责同步策略。
- 回调桥接：`JNIMediaPlayerListener` 通过 `postEventFromNative` 将事件回传到 `NativeLib.EventHandler`，再派发至 Java 层监听器。
- 渲染与输出：音频输出使用 OpenSLES（`AudioOpenSLESOutput`），视频渲染实现位于 `platform/android/renders/` 并通过 `IVideoRender` 抽象。

## 构建、测试与开发命令
在仓库根目录执行：
```bash
./gradlew clean
./gradlew :app:assembleDebug
./gradlew :app:installDebug
./gradlew test
./gradlew connectedAndroidTest
```
备注：`app` 默认仅打包 `arm64-v8a`，如需其他 ABI 请调整 `ndk.abiFilters`。

## 编码风格与命名约定
- Java/Kotlin 目标版本 1.8；保持 Android API 24+ 兼容。
- 使用 Android Studio 默认格式（4 空格缩进、同一行大括号）。
- 包名示例：`com.example.lzplayer`、`com.ctw.mediaselector`。
- 避免匿名内部类用于关键路径（历史上 DEX 问题已通过具名类规避）。

## 测试规范
- 单测：JUnit4；仪器测试：AndroidX Test + Espresso。
- 命名采用 `*Test`，放入对应的 `src/test/java` 或 `src/androidTest/java`。

## 提交与 PR 约定
- 提交信息简短直接（仓库历史多为中文动词短语，如“优化代码”、“完善接口”）。
- PR 建议包含：变更说明、测试命令与结果、UI 变更截图/录屏、关联 Issue（如有）。

## 环境与配置提示
- 需要 Android SDK 33 与 NDK 25.1.8937393（见根 `build.gradle`）。
- 请确保 `local.properties` 配置正确的 SDK/NDK 路径。
