#ifndef LZPLAYER_VENETWORKSOURCE_H
#define LZPLAYER_VENETWORKSOURCE_H

#include <memory>
#include "VEDemux.h"
#include "VEBufferedDataSource.h"

namespace VE {

    /// 网络媒体源：HTTP(S) 渐进式下载。
    ///
    /// 复用 VEDemux 的整套 demux 内核(读循环、节流、seek、包队列、命令面)，
    /// 只覆写"如何打开输入"——把自实现的 HTTP 客户端经自定义 AVIOContext
    /// 注入给 FFmpeg。**FFmpeg 只做 demux，一个字节的网络 IO 都不碰**，
    /// 这是项目既定的架构决策。
    class VENetworkSource : public VEDemux {
    public:
        explicit VENetworkSource(std::shared_ptr<AMessage> &notify);
        ~VENetworkSource() override;

        /// 同时中断 FFmpeg 与底层 socket：网络卡死时 teardown 仍然有界
        void abort() override;

    protected:
        VEResult openInput(AVFormatContext *ctx, const std::string &path) override;

        /// 网络源沿用 FFmpeg 默认探测上限。本地那套激进值(512KB/1s)在这里
        /// 不适用：字节要现拉，探测量不足会漏轨道或拿不到 fps——那是功能
        /// 缺陷，不是能拿来换启播速度的东西
        ProbeLimits probeLimits() const override {
            return ProbeLimits{0, 0, -1};
        }

        /// 网络场景要缓得更深：一次卡顿的代价远高于多占几十 MB 内存
        size_t maxTotalBytes() const override { return 64 * 1024 * 1024; }
        int64_t bufferedDurationTargetUs() const override { return 10 * 1000000; }

    private:
        /// FFmpeg 的 AVIO 回调，转调 VEBufferedDataSource
        static int avioRead(void *opaque, uint8_t *buf, int size);
        static int64_t avioSeek(void *opaque, int64_t offset, int whence);

        void onBufferingEvent(int event, int percent);

        std::shared_ptr<VEBufferedDataSource> mDataSource;
        AVIOContext *mAvio = nullptr;
        uint8_t *mAvioBuffer = nullptr;
        /// AVIO 的逻辑读位置(FFmpeg 用 seek 回调告诉我们它要读哪儿)
        int64_t mPosition = 0;
    };
}

#endif //LZPLAYER_VENETWORKSOURCE_H
