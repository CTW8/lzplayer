待实现功能：
1. 音频变速
2. 精准seek
3. rtmp拉流
4. 增加Android硬解码支持
5. 单音轨播放支持
6. 单视频播放支持
7. 音画同步支持三种时钟模式
8. 帧内存复用

框架改造：
1. VEPlayer与各个组件之间增加观察者模式，简化VEPlayer的流程
2. 框架层对ffmpeg的依赖进行剥离
3. 抽象统一的VEFrame，不在单独构建VEPacket
4. 增加组件之间connect机制



bug:
1. surfaceView销毁重建过程中视频播放失败的问题 **已修复，销毁时调用 releaseSurface()**
2. 完善demo

## 编译与运行
1. 推荐使用 **Android Studio Flamingo** 及以上版本，NDK r23c。
2. 克隆仓库后执行 `git submodule update --init --recursive` 准备依赖库（如 FFmpeg）。
3. 在项目根目录运行 `./gradlew assembleDebug` 或使用 Android Studio 直接编译安装。
4. 如编译失败，可检查 `lzplayer_core/src/main/cpp` 中 FFmpeg 路径配置是否正确。
