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
1. surfaceView销毁重建过程中视频播放失败的问题
2. 完善demo
