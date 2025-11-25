# LZ Player - Cross-Platform Video Player

一个基于自定义VEPlayer引擎的跨平台视频播放器应用。核心使用C++实现，架构参考Android NuPlayer设计，但摆脱平台依赖。

## 🎉 项目状态：BUILD SUCCESSFUL ✅

项目已成功解决所有编译问题，包括：
- ✅ KSP版本兼容性问题
- ✅ DEX编译空指针异常（完全解决）
- ✅ 内部类和匿名类导致的D8 dexer问题
- ✅ 原生模块编译问题
- ✅ 依赖版本冲突
- ✅ 字符串空指针引用问题
- ✅ MainActivity内部类结构优化
- ✅ 跨平台架构重构

**最终状态**: 应用已成功构建并安装到设备，所有模块正常工作！

## 🌍 跨平台架构

### 核心设计理念
本项目采用平台抽象层设计，核心代码与平台解耦：

- **解码**: 使用 FFmpeg avcodec 进行音视频解码
- **数据源**: 使用 FFmpeg avformat 进行媒体解复用
- **视频渲染**: 使用 OpenGL ES 进行跨平台视频渲染
- **消息处理**: 采用 NuPlayer 风格的 AHandler/ALooper 消息驱动架构

### 平台支持
| 平台 | 状态 | 渲染 | 音频 |
|------|------|------|------|
| Android | ✅ 完全支持 | OpenGL ES 3.0 | OpenSL ES |
| Linux | 🚧 规划中 | OpenGL | ALSA/PulseAudio |
| macOS | 🚧 规划中 | OpenGL | Core Audio |
| iOS | 🚧 规划中 | OpenGL ES | Core Audio |
| Windows | 🚧 规划中 | OpenGL | WASAPI |

### 平台抽象层
```
lzplayer_core/src/main/cpp/
├── core/              # 平台无关核心代码
│   ├── VEPlayer       # 主播放器控制器
│   ├── VEDemux        # FFmpeg avformat 解复用
│   ├── VEVideoDecoder # FFmpeg avcodec 视频解码
│   ├── VEAudioDecoder # FFmpeg avcodec 音频解码
│   └── VEAVsync       # 音视频同步
├── interface/         # 跨平台接口定义
│   ├── IVideoRender   # 视频渲染接口
│   ├── IAudioRender   # 音频渲染接口
│   └── IMediaSource   # 媒体源接口
├── platform/          # 平台特定实现
│   ├── VEPlatform.h   # 平台检测和通用类型
│   ├── android/       # Android平台实现
│   │   ├── renders/   # OpenGL ES渲染
│   │   └── decoders/  # MediaCodec硬解码
│   └── linux/         # Linux平台实现(规划中)
└── thread/            # 消息处理框架
    ├── AHandler       # 消息处理器
    ├── ALooper        # 消息循环
    └── AMessage       # 消息对象
```

## 功能特性

### 🎥 视频播放功能
- **文件选择**: 使用MediaSelector模块选择视频文件
- **播放控制**: Play/Pause/Stop/Resume完整播放控制
- **进度控制**: 可拖拽的进度条，支持视频跳转
- **时间显示**: 实时显示播放进度和总时长
- **状态监控**: 实时显示播放器状态
- **Surface渲染**: 硬件加速视频渲染

### 🏗️ 技术架构
- **VEPlayer**: 自定义C++视频播放引擎 (NuPlayer风格)
- **FFmpeg**: avformat解复用 + avcodec软解码
- **OpenGL ES**: 跨平台视频渲染
- **AHandler/ALooper**: 消息驱动异步架构
- **MediaSelector**: Kotlin编写的媒体文件选择器

## 项目结构

```
lzplayer/
├── app/                    # 主应用模块
├── lzplayer_core/         # VEPlayer核心播放引擎 (C++/JNI)
├── MediaSelector/         # 媒体文件选择器 (Kotlin)
├── MediaPipeline/         # 媒体管道处理
└── VERecorder/           # 视频录制模块
```

## 使用说明

### 1. 选择视频文件
- 点击"Select Video"按钮
- 从MediaSelector中选择视频文件
- 支持常见视频格式 (MP4, AVI, MKV等)

### 2. 播放控制
- **Play**: 开始播放视频
- **Pause**: 暂停播放
- **Resume**: 从暂停位置继续播放  
- **Stop**: 停止播放并重置进度

### 3. 进度控制
- 拖拽进度条跳转到指定位置
- 实时显示播放时间和总时长
- 支持精确的时间定位

## 技术实现

### 播放器架构
```
MainActivity (UI控制层)
├── VEPlayer (C++播放引擎)
├── MediaSelector (文件选择)
├── SurfaceView (视频渲染)
├── 播放控制 (Play/Pause/Stop/Resume)
├── 进度控制 (SeekBar + 时间显示)
└── 状态管理 (完整状态机)
```

### 关键技术点
- **DEX编译优化**: 避免匿名内部类，使用具名类
- **状态管理**: 完整的播放器状态机制
- **多线程安全**: UI线程和播放线程分离
- **内存管理**: 适当的资源释放和清理
- **错误处理**: 完善的异常处理机制

## 构建配置

### 版本信息
- **Kotlin**: 1.8.20
- **Android Gradle Plugin**: 7.4.2
- **KSP**: 1.8.20-1.0.11
- **Compile SDK**: 33
- **Target SDK**: 33
- **Min SDK**: 24
- **NDK**: 25.1.8937393

### 依赖管理
- AndroidX统一版本管理
- 兼容性依赖解决策略
- 原生模块CMake构建

## 开发环境

### 要求
- Android Studio Arctic Fox或更高版本
- JDK 8或更高版本
- Android SDK API 33
- NDK 25.1.8937393

### 构建命令
```bash
# 清理项目
./gradlew clean

# 构建Debug版本
./gradlew assembleDebug

# 安装到设备
./gradlew installDebug
```

## 问题解决历程

### 主要问题及解决方案

#### 1. KSP版本兼容性
- **问题**: Kotlin 2.0.0与KSP 1.9.20-1.0.14不兼容
- **解决**: 降级到Kotlin 1.8.20 + KSP 1.8.20-1.0.11

#### 2. DEX编译空指针异常
- **问题**: D8 dexer处理内部类时遇到null字符串引用
- **解决**: 
  - 消除所有匿名内部类
  - 使用具名内部类替代
  - 添加null安全检查

#### 3. 依赖版本冲突
- **问题**: AndroidX依赖版本不一致
- **解决**: 统一所有模块的依赖版本

#### 4. 原生模块编译
- **问题**: NDK版本和CMake配置问题
- **解决**: 更新NDK版本，优化CMake配置

## 许可证

本项目仅供学习和研究使用。

## 贡献

欢迎提交Issue和Pull Request来改进项目。

---

**项目状态**: ✅ 完全可用
**最后更新**: 2024年
**构建状态**: BUILD SUCCESSFUL
