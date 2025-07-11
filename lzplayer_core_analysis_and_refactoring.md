# LZPlayer Core 代码结构分析与平台无关化重构方案

## 当前代码结构分析

### 1. 总体架构
当前lzplayer_core采用了分层架构，主要包含以下组件：
- **VEPlayer**: 播放器主控制器
- **VEDemux**: 解复用器，负责从媒体文件中分离音视频流
- **VEAudioDecoder/VEVideoDecoder**: 音视频解码器
- **VEAudioRender/VEVideoRender**: 音视频渲染器
- **AudioOpenSLESOutput**: Android平台特定的音频输出

### 2. 现有接口抽象
项目已经定义了一些抽象接口：
- `IAudioRender`: 音频渲染器接口
- `IVideoRender`: 视频渲染器接口  
- `IMediaDecoder`: 媒体解码器接口
- `IVEComponent`: VE组件基础接口

### 3. 平台依赖分析

#### 3.1 平台无关组件（已实现）
- **VEDemux**: 基于FFmpeg，已基本平台无关
- **VEPacketQueue/VEFrameQueue**: 数据队列管理，平台无关
- **VEMediaClock/VEAVsync**: 时钟同步，平台无关

#### 3.2 需要平台无关化的组件
1. **VEAudioDecoder/VEVideoDecoder**: 
   - 当前直接使用FFmpeg，但包含一些平台特定的优化代码
   - 需要抽象解码器接口以支持硬件解码

2. **VEVideoRender**:
   - 直接包含EGL/OpenGL ES代码
   - 硬编码了Android ANativeWindow

3. **VEAudioRender**:
   - 依赖AudioOpenSLESOutput（Android特定）

4. **VEPlayer**:
   - 包含Android JNI和ANativeWindow相关代码

## 平台无关化重构方案

### 1. 架构设计原则

#### 1.1 分层架构
```
┌─────────────────────────────────────┐
│           Application Layer         │  (Java/Kotlin, Swift, C#等)
├─────────────────────────────────────┤
│        Platform Adapter Layer      │  (JNI, ObjC++, P/Invoke等)
├─────────────────────────────────────┤
│         Core Business Layer        │  (平台无关C++核心逻辑)
├─────────────────────────────────────┤
│       Platform Abstraction Layer   │  (平台抽象接口)
├─────────────────────────────────────┤
│      Platform Implementation       │  (平台特定实现)
└─────────────────────────────────────┘
```

#### 1.2 设计模式
- **Abstract Factory Pattern**: 创建平台特定的组件
- **Strategy Pattern**: 不同平台的渲染和解码策略
- **Observer Pattern**: 状态通知和事件处理

### 2. 核心接口重构

#### 2.1 媒体解复用器接口（IMediaDemuxer）
```cpp
namespace VE {
    enum class StreamType { VIDEO, AUDIO, SUBTITLE };
    
    struct MediaInfo {
        int64_t duration;
        std::vector<StreamInfo> streams;
    };
    
    class IMediaDemuxer {
    public:
        virtual ~IMediaDemuxer() = default;
        virtual VEResult open(const std::string& url) = 0;
        virtual VEResult getMediaInfo(MediaInfo& info) = 0;
        virtual VEResult readPacket(StreamType type, std::shared_ptr<VEPacket>& packet) = 0;
        virtual VEResult seek(int64_t timestampMs) = 0;
        virtual VEResult close() = 0;
    };
}
```

#### 2.2 解码器接口（IDecoder）
```cpp
namespace VE {
    enum class CodecType { H264, H265, VP8, VP9, AV1, AAC, MP3, OPUS };
    enum class DecoderType { SOFTWARE, HARDWARE };
    
    struct DecoderConfig {
        CodecType codec;
        DecoderType type;
        // 视频特定参数
        int width = 0;
        int height = 0;
        // 音频特定参数  
        int sampleRate = 0;
        int channels = 0;
        // 额外数据
        std::vector<uint8_t> extraData;
    };
    
    class IDecoder {
    public:
        virtual ~IDecoder() = default;
        virtual VEResult configure(const DecoderConfig& config) = 0;
        virtual VEResult start() = 0;
        virtual VEResult decode(const std::shared_ptr<VEPacket>& packet) = 0;
        virtual VEResult getFrame(std::shared_ptr<VEFrame>& frame) = 0;
        virtual VEResult flush() = 0;
        virtual VEResult stop() = 0;
        virtual VEResult release() = 0;
    };
    
    class IAudioDecoder : public IDecoder {
        // 音频解码器特定接口
    };
    
    class IVideoDecoder : public IDecoder {
        // 视频解码器特定接口
    };
}
```

#### 2.3 渲染器接口（IRender）
```cpp
namespace VE {
    struct RenderConfig {
        void* platformData;  // 平台特定数据（如Window Handle, Surface等）
        int width;
        int height;
        int fps;
    };
    
    class IRender {
    public:
        virtual ~IRender() = default;
        virtual VEResult configure(const RenderConfig& config) = 0;
        virtual VEResult start() = 0;
        virtual VEResult renderFrame(const std::shared_ptr<VEFrame>& frame) = 0;
        virtual VEResult stop() = 0;
        virtual VEResult release() = 0;
    };
    
    class IVideoRender : public IRender {
    public:
        virtual VEResult setSurface(void* surface, int width, int height) = 0;
    };
    
    class IAudioRender : public IRender {
    public:
        virtual VEResult setVolume(float volume) = 0;
    };
}
```

### 3. 平台抽象层设计

#### 3.1 平台工厂接口
```cpp
namespace VE {
    enum class Platform { ANDROID, IOS, WINDOWS, MACOS, LINUX };
    
    class IPlatformFactory {
    public:
        virtual ~IPlatformFactory() = default;
        virtual std::shared_ptr<IMediaDemuxer> createDemuxer() = 0;
        virtual std::shared_ptr<IAudioDecoder> createAudioDecoder(CodecType codec) = 0;
        virtual std::shared_ptr<IVideoDecoder> createVideoDecoder(CodecType codec) = 0;
        virtual std::shared_ptr<IAudioRender> createAudioRender() = 0;
        virtual std::shared_ptr<IVideoRender> createVideoRender() = 0;
        virtual Platform getPlatform() const = 0;
    };
    
    class PlatformManager {
    public:
        static void registerFactory(Platform platform, std::shared_ptr<IPlatformFactory> factory);
        static std::shared_ptr<IPlatformFactory> getFactory(Platform platform);
        static std::shared_ptr<IPlatformFactory> getCurrentFactory();
    };
}
```

#### 3.2 Android平台实现
```cpp
namespace VE {
    class AndroidPlatformFactory : public IPlatformFactory {
    public:
        std::shared_ptr<IMediaDemuxer> createDemuxer() override {
            return std::make_shared<FFmpegDemuxer>();
        }
        
        std::shared_ptr<IAudioDecoder> createAudioDecoder(CodecType codec) override {
            // 优先尝试硬件解码，失败则回退到软件解码
            auto decoder = std::make_shared<MediaCodecAudioDecoder>();
            if (decoder->isSupported(codec)) {
                return decoder;
            }
            return std::make_shared<FFmpegAudioDecoder>();
        }
        
        std::shared_ptr<IVideoDecoder> createVideoDecoder(CodecType codec) override {
            auto decoder = std::make_shared<MediaCodecVideoDecoder>();
            if (decoder->isSupported(codec)) {
                return decoder;
            }
            return std::make_shared<FFmpegVideoDecoder>();
        }
        
        std::shared_ptr<IAudioRender> createAudioRender() override {
            return std::make_shared<OpenSLESAudioRender>();
        }
        
        std::shared_ptr<IVideoRender> createVideoRender() override {
            return std::make_shared<OpenGLESVideoRender>();
        }
        
        Platform getPlatform() const override { return Platform::ANDROID; }
    };
}
```

### 4. 核心播放器重构

#### 4.1 VEPlayer重构
```cpp
namespace VE {
    class VEPlayer : public AHandler {
    public:
        VEPlayer(std::shared_ptr<IPlatformFactory> factory = nullptr);
        
        // 设置数据源
        VEResult setDataSource(const std::string& url);
        
        // 设置视频输出表面
        VEResult setVideoSurface(void* surface, int width, int height);
        
        // 播放控制
        VEResult prepare();
        VEResult start();
        VEResult pause();
        VEResult stop();
        VEResult seekTo(int64_t timestampMs);
        VEResult release();
        
        // 获取播放状态
        int64_t getCurrentPosition() const;
        int64_t getDuration() const;
        
        // 设置回调
        void setOnPreparedListener(const std::function<void()>& listener);
        void setOnErrorListener(const std::function<void(int, const std::string&)>& listener);
        void setOnCompletionListener(const std::function<void()>& listener);
        
    private:
        std::shared_ptr<IPlatformFactory> mPlatformFactory;
        std::shared_ptr<IMediaDemuxer> mDemuxer;
        std::shared_ptr<IAudioDecoder> mAudioDecoder;
        std::shared_ptr<IVideoDecoder> mVideoDecoder;
        std::shared_ptr<IAudioRender> mAudioRender;
        std::shared_ptr<IVideoRender> mVideoRender;
        
        // 其他成员变量...
    };
}
```

### 5. 目录结构重构

```
lzplayer_core/
├── src/main/cpp/
│   ├── core/                          # 核心业务逻辑（平台无关）
│   │   ├── player/
│   │   │   ├── VEPlayer.h/cpp        # 重构后的播放器
│   │   │   ├── VEPlayerState.h       # 播放器状态管理
│   │   │   └── VEPlayerContext.h     # 播放器上下文
│   │   ├── demux/
│   │   │   └── FFmpegDemuxer.h/cpp   # FFmpeg解复用器实现
│   │   ├── decoder/
│   │   │   ├── FFmpegAudioDecoder.h/cpp
│   │   │   └── FFmpegVideoDecoder.h/cpp
│   │   ├── render/
│   │   │   ├── AudioRenderer.h/cpp    # 平台无关音频渲染逻辑
│   │   │   └── VideoRenderer.h/cpp    # 平台无关视频渲染逻辑
│   │   ├── sync/
│   │   │   ├── VEAVSync.h/cpp        # 音视频同步
│   │   │   └── VEMediaClock.h/cpp    # 媒体时钟
│   │   └── utils/                     # 工具类
│   ├── interface/                     # 抽象接口定义
│   │   ├── IMediaDemuxer.h
│   │   ├── IDecoder.h
│   │   ├── IAudioDecoder.h
│   │   ├── IVideoDecoder.h
│   │   ├── IRender.h
│   │   ├── IAudioRender.h
│   │   ├── IVideoRender.h
│   │   └── IPlatformFactory.h
│   ├── platform/                      # 平台特定实现
│   │   ├── common/                    # 通用平台实现
│   │   │   ├── FFmpegDemuxer.h/cpp
│   │   │   ├── FFmpegAudioDecoder.h/cpp
│   │   │   └── FFmpegVideoDecoder.h/cpp
│   │   ├── android/
│   │   │   ├── AndroidPlatformFactory.h/cpp
│   │   │   ├── MediaCodecDecoder.h/cpp
│   │   │   ├── OpenSLESAudioRender.h/cpp
│   │   │   └── OpenGLESVideoRender.h/cpp
│   │   ├── ios/
│   │   │   ├── IOSPlatformFactory.h/cpp
│   │   │   ├── VideoToolboxDecoder.h/cpp
│   │   │   ├── AudioUnitRender.h/cpp
│   │   │   └── MetalVideoRender.h/cpp
│   │   ├── windows/
│   │   │   ├── WindowsPlatformFactory.h/cpp
│   │   │   ├── MediaFoundationDecoder.h/cpp
│   │   │   ├── WASAPIAudioRender.h/cpp
│   │   │   └── D3D11VideoRender.h/cpp
│   │   └── linux/
│   │       ├── LinuxPlatformFactory.h/cpp
│   │       ├── VAAPIDecoder.h/cpp
│   │       ├── ALSAAudioRender.h/cpp
│   │       └── OpenGLVideoRender.h/cpp
│   ├── thread/                        # 线程管理（保持不变）
│   └── utils/                         # 工具类（保持不变）
```

### 6. 实施步骤

#### 阶段1: 接口抽象和基础架构
1. 定义新的抽象接口
2. 实现平台工厂模式
3. 重构VEPlayer使用抽象接口

#### 阶段2: 解复用器重构
1. 创建IMediaDemuxer接口
2. 将现有VEDemux重构为FFmpegDemuxer
3. 更新VEPlayer使用新的解复用器接口

#### 阶段3: 解码器重构  
1. 创建解码器抽象接口
2. 重构现有解码器为FFmpeg实现
3. 添加Android MediaCodec解码器实现
4. 实现解码器选择策略

#### 阶段4: 渲染器重构
1. 创建渲染器抽象接口
2. 重构现有渲染器为Android实现
3. 实现跨平台渲染器管理

#### 阶段5: 其他平台支持
1. 添加iOS平台实现
2. 添加Windows平台实现
3. 添加Linux平台实现

### 7. 预期收益

1. **跨平台支持**: 一套核心代码支持多个平台
2. **可维护性**: 清晰的架构分层，易于维护和扩展
3. **可测试性**: 接口抽象使得单元测试更容易
4. **性能优化**: 可以为不同平台选择最优的实现
5. **硬件加速**: 支持各平台的硬件解码和渲染

### 8. 注意事项

1. **向后兼容**: 保持现有API的兼容性
2. **性能影响**: 抽象层不应显著影响性能
3. **内存管理**: 注意跨平台的内存管理差异
4. **线程安全**: 确保多线程环境下的安全性
5. **错误处理**: 统一的错误处理和异常机制

这个重构方案将使lzplayer_core成为一个真正跨平台的媒体播放器引擎，为不同平台提供一致的API和优化的性能。