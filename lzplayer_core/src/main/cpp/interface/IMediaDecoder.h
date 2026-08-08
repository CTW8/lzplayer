#ifndef LZPLAYER_IMEDIADECODER_H
#define LZPLAYER_IMEDIADECODER_H

#include <memory>
#include "IVEComponent.h"
#include "utils/VEStartupTrace.h"
#include "utils/VEPerfStats.h"
#include "IMediaSource.h"
#include "IFrameSink.h"
#include "VEBundle.h"

namespace VE {

    /// 解码器的统一接口：软解(FFmpeg)与硬解(MediaCodec)可互换。
    ///
    /// 数据面固定为"从 source 拉包 → 解码 → 推给 sink(带 credit 回执)"，
    /// 所以工厂可以按 codec/分辨率/策略选具体实现，运行期出错还能热替换
    /// 成软解——播放器只认这个接口，不感知背后是谁。
    class IMediaDecoder : public IVEComponent {
    public:
        ~IMediaDecoder() override = default;

        /// 建链。异步执行(投消息进解码器自己的 looper)，失败经 notify 上报 ERROR。
        /// params 由调用方按解码器类型填：音频用 outSampleRate/outChannels/outFormat；
        /// 视频硬解用 surface。不认识的键实现方直接忽略。
        virtual VEResult prepare(std::shared_ptr<IMediaSource> source,
                                 std::shared_ptr<IFrameSink> sink,
                                 const VEBundle &params) = 0;

        /// 注入启播里程碑记录器(T4a/T6，硬解还负责 T7)。
        /// 非纯虚：不关心启播耗时的解码器实现无需覆写
        virtual void setStartupTrace(const std::shared_ptr<VEStartupTrace> &trace) {
            (void) trace;
        }

        /// 注入稳态指标容器(解码耗时；硬解还负责上屏耗时与同步余量)
        virtual void setPerfStats(const std::shared_ptr<VEPerfStats> &stats) {
            (void) stats;
        }
    };
}

#endif //LZPLAYER_IMEDIADECODER_H
