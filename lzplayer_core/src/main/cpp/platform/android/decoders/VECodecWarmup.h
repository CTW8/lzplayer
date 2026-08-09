#ifndef LZPLAYER_VECODECWARMUP_H
#define LZPLAYER_VECODECWARMUP_H

#include <media/NdkMediaCodec.h>

namespace VE {

    /// MediaCodec 实例预热。
    ///
    /// 起因是实测：硬解 `configure` 总耗时 64~87ms 里，`AMediaCodec_configure`
    /// 只占 5.6~7.7ms，**`AMediaCodec_createDecoderByType` 独占 50~66ms**
    /// (76~78%)——那是组件加载 + binder 到 codec2 服务的成本。
    ///
    /// 关键在于创建实例**只依赖 mime**，不依赖轨道尺寸也不依赖 csd。所以它
    /// 可以在 `avformat_open_input` 一返回(约 5ms 处，codec_id 此时已知)就
    /// 在后台线程启动，完整藏进随后 `find_stream_info` 那 51~66ms 里。
    /// 不是猜 mime，是拿到了准确的 codec_id 才预热。
    ///
    /// 生命周期纪律(这类系统资源有限，握着不用会挤掉别人的分配)：
    /// - 预热的实例只保留一个槽位，mime 不匹配时直接丢弃重建
    /// - 取用后槽位立即置空，所有权移交调用方
    /// - 播放器释放时必须调 discard() 兜底，避免没人来取的实例长期占着
    class VECodecWarmup {
    public:
        /// 按 FFmpeg codec_id 预热。非硬解白名单内的编码直接忽略。
        /// 可从任意线程调用，立即返回（真正的创建在后台线程）。
        static void warmUpForCodec(int avCodecId);

        /// 取走已预热的实例。mime 不匹配返回 nullptr(调用方照常自建)。
        /// 若预热仍在进行中会**等它完成**——等待也比从零创建快，
        /// 因为那 50ms 已经走了一部分。
        static AMediaCodec *take(const char *mime);

        /// 丢弃未被取用的实例。播放器释放路径上必须调。
        static void discard();

        /// codec_id → MediaCodec mime。白名单之外返回 nullptr。
        /// 这份映射只放这一处，解码器侧也用它，避免两份各自演进。
        static const char *mimeForCodec(int avCodecId);
    };
}

#endif //LZPLAYER_VECODECWARMUP_H
