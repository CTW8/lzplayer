# LZ Player - Android Video Player

一个基于自定义VEPlayer引擎的Android视频播放器应用。

## 🎉 项目状态：BUILD SUCCESSFUL ✅

项目已成功解决所有编译问题，包括：
- ✅ KSP版本兼容性问题
- ✅ DEX编译空指针异常（NativeLib$EventHandler）
- ✅ 内部类和枚举导致的D8 dexer问题
- ✅ 原生模块编译问题
- ✅ 依赖版本冲突
- ✅ 字符串空指针引用问题

**最终状态**: 应用已成功构建并安装到设备，所有模块正常工作！

## 功能特性

### 🎥 视频播放功能
- **文件选择**: 使用MediaSelector模块选择视频文件
- **播放控制**: Play/Pause/Stop/Resume完整播放控制
- **进度控制**: 可拖拽的进度条，支持视频跳转
- **时间显示**: 实时显示播放进度和总时长
- **状态监控**: 实时显示播放器状态

### 🏗️ 技术架构
- **VEPlayer**: 自定义C++视频播放引擎
- **MediaSelector**: Kotlin编写的媒体文件选择器
- **Surface渲染**: 使用SurfaceView进行视频渲染
- **多线程**: 异步播放和进度更新

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

## 技术规格

### 开发环境
- **Android Studio**: 2023.3.1+
- **Gradle**: 8.12
- **AGP**: 7.4.2
- **Kotlin**: 1.8.20
- **NDK**: 25.1.8937393

### 依赖版本
- **KSP**: 1.8.20-1.0.11
- **compileSdk**: 33
- **targetSdk**: 33
- **minSdk**: 24

### 支持架构
- **ARM64**: arm64-v8a (主要支持)
- **原生库**: FFmpeg + 自定义播放引擎

## 构建说明

### 1. 环境准备
```bash
# 确保安装了正确的NDK版本
# Android Studio -> SDK Manager -> NDK (Side by side) -> 25.1.8937393
```

### 2. 构建项目
```bash
# 清理构建
./gradlew clean

# 构建Debug版本
./gradlew assembleDebug

# 安装到设备
./gradlew installDebug
```

### 3. 常见问题解决

#### DEX编译错误
如果遇到"Cannot invoke String.length() because parameter1 is null"错误：
- 已在NativeLib.java中添加空指针检查
- 确保所有字符串参数都有null安全处理

#### 原生编译警告
C++编译过程中的警告是正常的，不影响功能：
- GNU扩展警告：使用了GCC特定的宏扩展
- 类型转换警告：FFmpeg库的类型转换
- 移动构造函数警告：C++11标准相关

## 项目亮点

### 1. 完整的错误处理
- DEX编译时的空指针安全
- 播放器状态机完整性检查
- 原生代码异常捕获

### 2. 现代Android开发
- 使用ActivityResultLauncher替代过时的startActivityForResult
- 支持Android 13+ (API 33)
- 遵循Material Design规范

### 3. 高性能视频播放
- 硬件加速渲染
- 多线程解码
- 低延迟播放控制

### 4. 模块化设计
- 独立的媒体选择器模块
- 可复用的播放引擎
- 清晰的模块边界

## 开发者说明

### 代码结构
- **MainActivity.java**: 主界面和播放控制逻辑
- **VEPlayer.java**: 播放器接口封装
- **NativeLib.java**: JNI桥接层（已修复空指针问题）
- **MediaSelector**: 独立的文件选择模块

### 关键修复
1. **NativeLib EventHandler**: 添加了字符串null检查
2. **MainActivity**: 避免了匿名内部类导致的DEX问题
3. **依赖管理**: 统一了所有模块的版本兼容性

## 许可证

本项目仅供学习和研究使用。

---

**构建状态**: ✅ SUCCESS  
**最后更新**: 2024年  
**测试设备**: Android 13+ (API 33)
