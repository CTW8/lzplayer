#ifndef LZPLAYER_VEBUFFEREDDATASOURCE_H
#define LZPLAYER_VEBUFFEREDDATASOURCE_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "IDataSource.h"

namespace VE {

    /// 预取 + 环形缓存 + 水位判定。
    ///
    /// 独立预取线程持续把字节从下游 IDataSource 拉进环形缓存；demux 线程
    /// 经 readAt 消费。这样 FFmpeg 的 read 回调大多数时候是内存拷贝，
    /// 不会因为一次网络往返把整条 demux 链路挂住。
    ///
    /// 卡顿判定唯一发生在这一层，上层(播放器)据此暂停/恢复数据面。
    ///
    /// ⚠ **实际策略与下面 Config 的字段名并不一致，别照字段名理解**：
    ///   BUFFERING_START 在 `readAt` **已经被迫阻塞**的那一刻才发(缓冲在所需
    ///     偏移处已经干了)，**没有任何余量** —— 不是"跌破低水位"就预警；
    ///   BUFFERING_END 才是真的按水位发(resumeWaterBytes，由预取线程发出)。
    /// 见 startWaterBytes / lowWaterBytes 两个字段的说明。
    class VEBufferedDataSource : public IDataSource {
    public:
        struct Config {
            size_t cacheBytes = 32 * 1024 * 1024;   ///< 环形缓存容量
            /// ⚠ **未实现，全工程零引用。** 保留是为了记住设计意图，
            /// 不是因为它在生效 —— 当前**没有起播水位**这回事，demux 读得到
            /// 就开始播。把它当成"已经攒够 2MB 才起播"会得出错误结论。
            size_t startWaterBytes = 2 * 1024 * 1024;
            /// ⚠ **未实现，全工程零引用。** 当前 BUFFERING_START 是在 readAt
            /// 被迫阻塞时才发的，即"已经饿了"而不是"快饿了"，零预警余量。
            /// 实现它会让卡顿事件变多(提前上报)，属行为变更，需先定夺。
            size_t lowWaterBytes = 256 * 1024;
            size_t resumeWaterBytes = 1024 * 1024;  ///< 回到此值恢复
            /// 前向未命中时最多等预取赶多远；超过就重定位(Range 重开)
            int64_t forwardSkipMax = 512 * 1024;
        };

        /// 缓冲状态回调(在预取线程或消费线程上触发，实现方只应投消息)
        using BufferingCallback = std::function<void(int event, int percent)>;

        VEBufferedDataSource(std::shared_ptr<IDataSource> upstream, const Config &config);
        ~VEBufferedDataSource() override;

        void setBufferingCallback(BufferingCallback cb) { mOnBuffering = std::move(cb); }

        VEResult open(const std::string &url, int64_t offset) override;
        ssize_t readAt(int64_t offset, void *buf, size_t size) override;
        int64_t size() const override;
        void abort() override;
        void close() override;

        /// 缓冲进度百分比(已下载 / 总长)，未知长度时返回 -1
        int bufferedPercent() const;

    private:
        void prefetchLoop();
        /// 丢弃缓存并让下游从 offset 重新拉(跨区 seek 时)。
        /// 取 unique_lock 的引用而不是裸 mutex：中途要放锁 join 预取线程，
        /// 放锁必须经 lock 对象走，否则它的 owns 状态与实际锁状态会脱节。
        VEResult reposition(std::unique_lock<std::mutex> &lk, int64_t offset);
        size_t availableFromLocked(int64_t offset) const;
        void notifyBuffering(int event, int percent);

        std::shared_ptr<IDataSource> mUpstream;
        Config mConfig;
        BufferingCallback mOnBuffering;

        std::vector<uint8_t> mCache;
        /// 缓存窗口 [mCacheStart, mCacheEnd) 对应的绝对文件偏移
        int64_t mCacheStart = 0;
        int64_t mCacheEnd = 0;
        /// mCacheStart 在环形缓冲里的下标
        size_t mHead = 0;

        std::string mUrl;
        int64_t mTotalSize = -1;

        mutable std::mutex mMutex;
        std::condition_variable mDataAvailable;   ///< 预取线程填了数据
        std::condition_variable mSpaceAvailable;  ///< 消费者腾出了空间

        std::thread mPrefetchThread;
        std::atomic<bool> mRunning{false};
        std::atomic<bool> mAbort{false};
        /// 下游已到流尾，别再等预取
        bool mUpstreamEof = false;
        bool mBuffering = false;
        /// 预取线程遇到的错误(消费侧据此返回失败而不是死等)
        VEResult mPrefetchError = VE_OK;
    };
}

#endif //LZPLAYER_VEBUFFEREDDATASOURCE_H
