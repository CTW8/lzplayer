#include "VEBufferedDataSource.h"
#include <unistd.h>
#include "utils/VEPerfStats.h"   // nowUs()

#include <algorithm>
#include <cstring>

#include "utils/Log.h"
#include "VEDef.h"

namespace VE {
    namespace {
        /// 单次从下游拉取的块大小
        constexpr size_t kFetchChunk = 64 * 1024;
        /// 等预取赶上时的轮询粒度(同时也是 abort 的生效上限)
        constexpr int kWaitSliceMs = 100;
    }

    VEBufferedDataSource::VEBufferedDataSource(std::shared_ptr<IDataSource> upstream,
                                               const Config &config)
            : mUpstream(std::move(upstream)), mConfig(config) {
        mCache.resize(mConfig.cacheBytes);
    }

    VEBufferedDataSource::~VEBufferedDataSource() {
        close();
    }

    VEResult VEBufferedDataSource::open(const std::string &url, int64_t offset) {
        mUrl = url;
        mAbort = false;

        const VEResult ret = mUpstream->open(url, offset);
        if (ret != VE_OK) {
            return ret;
        }
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mTotalSize = mUpstream->size();
            mCacheStart = offset;
            mCacheEnd = offset;
            mHead = 0;
            mUpstreamEof = false;
            mPrefetchError = VE_OK;
        }
        mRunning = true;
        mPrefetchThread = std::thread(&VEBufferedDataSource::prefetchLoop, this);
        return VE_OK;
    }

    void VEBufferedDataSource::prefetchLoop() {
        ALOGW("VEBufferedDataSource::prefetchLoop ENTER tid=%d", (int) gettid());
        pthread_setname_np(pthread_self(), "ve_prefetch");
        std::vector<uint8_t> chunk(kFetchChunk);

        while (mRunning && !mAbort) {
            size_t space;
            int64_t fetchAt;
            {
                std::unique_lock<std::mutex> lk(mMutex);
                // 缓存满就歇着，等消费者腾出空间
                while (mRunning && !mAbort &&
                       (mCacheEnd - mCacheStart) >= static_cast<int64_t>(mConfig.cacheBytes)) {
                    {
                        // 验证"预取等空间 / 空间靠 readAt 释放 / readAt 等预取
                        // 数据"是否成环。限流每秒一条
                        static int64_t sLastUs = 0;
                        const int64_t nowU = nowUs();
                        if (nowU - sLastUs > 1000000) {
                            sLastUs = nowU;
                            ALOGW("prefetch WAIT space start=%lld end=%lld used=%lld cap=%zu",
                                  (long long) mCacheStart, (long long) mCacheEnd,
                                  (long long) (mCacheEnd - mCacheStart), mConfig.cacheBytes);
                        }
                    }
                    mSpaceAvailable.wait_for(lk, std::chrono::milliseconds(kWaitSliceMs));
                }
                if (!mRunning || mAbort || mUpstreamEof) {
                    break;
                }
                space = mConfig.cacheBytes - static_cast<size_t>(mCacheEnd - mCacheStart);
                fetchAt = mCacheEnd;
                {
                    static int64_t sUs = 0;
                    const int64_t n = nowUs();
                    if (n - sUs > 1000000) {
                        sUs = n;
                        ALOGW("STATE prefetch tid=%d fetchAt=%lld space=%zu "
                              "start=%lld end=%lld head=%zu running=%d eof=%d",
                              (int) gettid(), (long long) fetchAt, space,
                              (long long) mCacheStart, (long long) mCacheEnd,
                              mHead, (int) mRunning, (int) mUpstreamEof);
                    }
                }
            }

            const size_t want = std::min(space, chunk.size());
            const ssize_t got = mUpstream->readAt(fetchAt, chunk.data(), want);
            {
                static int64_t sUs2 = 0;
                const int64_t n2 = nowUs();
                if (n2 - sUs2 > 1000000) {
                    sUs2 = n2;
                    ALOGW("STATE upstream tid=%d at=%lld want=%zu got=%zd",
                          (int) gettid(), (long long) fetchAt, want, got);
                }
            }

            std::lock_guard<std::mutex> lk(mMutex);
            if (got > 0) {
                // 写进环形缓冲：可能跨越尾部要分两段
                const size_t writePos = (mHead + static_cast<size_t>(mCacheEnd - mCacheStart)) %
                                        mConfig.cacheBytes;
                const size_t first = std::min(static_cast<size_t>(got),
                                              mConfig.cacheBytes - writePos);
                memcpy(mCache.data() + writePos, chunk.data(), first);
                if (static_cast<size_t>(got) > first) {
                    memcpy(mCache.data(), chunk.data() + first,
                           static_cast<size_t>(got) - first);
                }
                mCacheEnd += got;
                mDataAvailable.notify_all();
            } else if (got == 0) {
                ALOGW("VEBufferedDataSource::%s upstream EOF at %lld "
                      "tid=%d start=%lld end=%lld", __FUNCTION__,
                      static_cast<long long>(fetchAt), (int) gettid(),
                      (long long) mCacheStart, (long long) mCacheEnd);
                mUpstreamEof = true;
                mDataAvailable.notify_all();
                break;
            } else {
                ALOGE("VEBufferedDataSource::%s upstream read error", __FUNCTION__);
                mPrefetchError = VE_PLAYER_ERROR_NETWORK_IO;
                mDataAvailable.notify_all();
                break;
            }
        }
        ALOGI("VEBufferedDataSource::%s prefetch thread exit", __FUNCTION__);
    }

    size_t VEBufferedDataSource::availableFromLocked(int64_t offset) const {
        if (offset < mCacheStart || offset > mCacheEnd) {
            return 0;
        }
        return static_cast<size_t>(mCacheEnd - offset);
    }

    VEResult VEBufferedDataSource::reposition(std::unique_lock<std::mutex> &lk,
                                              int64_t offset) {
        // 停预取 → 重开下游 Range → 清缓存 → 重启预取。
        // 只在真正的跨区 seek 时发生，顺序播放不会走到。
        ALOGI("VEBufferedDataSource::%s reposition to %lld", __FUNCTION__,
              static_cast<long long>(offset));
        mRunning = false;
        mSpaceAvailable.notify_all();
        mDataAvailable.notify_all();

        // join 与网络重连都可能耗时，期间必须放锁(预取线程退出前还要拿锁)
        lk.unlock();
        if (mPrefetchThread.joinable()) {
            mPrefetchThread.join();
        }
        ALOGW("VEBufferedDataSource::reposition open BEGIN off=%lld tid=%d",
              (long long) offset, (int) gettid());
        const VEResult ret = mUpstream->open(mUrl, offset);
        ALOGW("VEBufferedDataSource::reposition open END ret=%d", (int) ret);
        lk.lock();

        if (ret != VE_OK) {
            mPrefetchError = ret;
            return ret;
        }
        mCacheStart = offset;
        mCacheEnd = offset;
        mHead = 0;
        mUpstreamEof = false;
        mPrefetchError = VE_OK;
        mRunning = true;
        // 一次运行就能看清有几个预取线程、各自什么状态。此前三次机制推断
        // 全被实测推翻(readAt 混淆两种 0 / EOF 未复位 / 线程生命周期),
        // 不再靠读代码猜
        ALOGW("VEBufferedDataSource::reposition to %lld, spawning prefetch "
              "(caller tid=%d)", (long long) offset, (int) gettid());
        mPrefetchThread = std::thread(&VEBufferedDataSource::prefetchLoop, this);
        return VE_OK;
    }

    namespace {
        /// 左沿之后保留的回读窗口。mp4 交错读与 avformat 内部回退都在这个
        /// 量级；给太小会重新触发 reposition 死循环，给太大则挤占前向缓冲
        constexpr size_t kKeepBackBytes = 4 * 1024 * 1024;
    }

    ssize_t VEBufferedDataSource::readAt(int64_t offset, void *buf, size_t size) {
        std::unique_lock<std::mutex> lk(mMutex);

        {
            // 缓存区间到底怎么走的。限流每秒一条, 否则打爆配额
            static int64_t sLastUs = 0;
            const int64_t nowU = nowUs();
            if (nowU - sLastUs > 1000000) {
                sLastUs = nowU;
                ALOGW("VEBufferedDataSource::readAt off=%lld size=%zu "
                      "start=%lld end=%lld avail=%zu eof=%d",
                      (long long) offset, size, (long long) mCacheStart,
                      (long long) mCacheEnd, availableFromLocked(offset),
                      (int) mUpstreamEof);
            }
        }
        if (offset < mCacheStart ||
            offset > mCacheEnd + mConfig.forwardSkipMax) {
            // 后向 seek，或前向跨得太远等不起 → 重定位
            if (reposition(lk, offset) != VE_OK) {
                return -1;
            }
        }

        // 等预取把这一段填上来
        while (availableFromLocked(offset) == 0) {
            // 左沿越过了自己要的位置 → 数据已被丢弃, **再等也等不来**:
            // 预取只让 mCacheEnd 往前长, mCacheStart 从不后退, 循环条件
            // 永远不会变真。函数入口那次 `offset < mCacheStart` 检查只在
            // 进入时做一次, 而 readAt 是多线程并发的(buffering 乱序已经
            // 证明这一点) —— 线程 A 等数据期间, 线程 B 推进左沿越过 A 的
            // offset, A 就永久卡死。
            // 实测表现: 停在 BUFFERING_START、END 再不出现、VESTAT 全无。
            if (offset < mCacheStart) {
                if (reposition(lk, offset) != VE_OK) {
                    return -1;
                }
                continue;
            }
            if (mPrefetchError != VE_OK) {
                return -1;
            }
            if (mUpstreamEof) {
                return 0;   // 真的没有更多数据了
            }
            if (!mBuffering) {
                // 消费者要的数据还没到 = 卡顿。上报后上层会暂停数据面。
                //
                // **状态变更与投递必须原子**, 不能解锁后再投递:
                // readAt 会被多个线程并发调用(FFmpeg 在 find_stream_info
                // 期间也读)。解锁窗口里另一个线程可以看到 mBuffering=true、
                // 判定水位已够、置 false 并投出 END —— END 就跑到 START
                // 前面, 上层收到乱序事件后把数据面永久留在暂停态。
                // 实测: END 在 01:44:34.103、START 在 .104, 各一次。
                //
                // 投递是 msg->post(), 非阻塞, 在锁内做是安全的。
                mBuffering = true;
                notifyBuffering(VE_NOTIFY_EVENT_BUFFERING_START, bufferedPercent());
            }
            {
                static int64_t sUs3 = 0;
                const int64_t n3 = nowUs();
                if (n3 - sUs3 > 1000000) {
                    sUs3 = n3;
                    ALOGW("STATE readAt-wait tid=%d off=%lld start=%lld end=%lld "
                          "head=%zu buffering=%d eof=%d err=%d",
                          (int) gettid(), (long long) offset,
                          (long long) mCacheStart, (long long) mCacheEnd, mHead,
                          (int) mBuffering, (int) mUpstreamEof, (int) mPrefetchError);
                }
            }
            mDataAvailable.wait_for(lk, std::chrono::milliseconds(kWaitSliceMs));
        }

        // 卡顿恢复要等回到恢复水位，避免在低水位反复抖动
        if (mBuffering) {
            const size_t avail = availableFromLocked(offset);
            if (avail >= mConfig.resumeWaterBytes || mUpstreamEof) {
                // 同上: 状态变更与投递在同一临界区内完成, 保证 START/END
                // 的投递顺序与状态变更顺序一致
                mBuffering = false;
                notifyBuffering(VE_NOTIFY_EVENT_BUFFERING_END, bufferedPercent());
            }
        }

        const size_t avail = availableFromLocked(offset);
        const size_t toCopy = std::min(size, avail);
        const size_t readPos = (mHead + static_cast<size_t>(offset - mCacheStart)) %
                               mConfig.cacheBytes;
        const size_t first = std::min(toCopy, mConfig.cacheBytes - readPos);
        memcpy(buf, mCache.data() + readPos, first);
        if (toCopy > first) {
            memcpy(static_cast<uint8_t *>(buf) + first, mCache.data(), toCopy - first);
        }

        // 左沿**按需**前移，且保留一段回读窗口。
        //
        // 原实现每读一次就把左沿推到本次读取末尾，前提是"demux 不会回头读
        // 已消费的数据(真要回头就是 seek，走重定位)"。**这个前提是错的**：
        // mp4 解析天然要回头 —— 读完 moov 索引跳回数据区、交错读音视频轨时
        // 在两个位置间来回、avformat 内部还有自己的缓冲回退。
        //
        // 实测(2026-08-21)：请求 off=5507278 落在 start=6052211 之前 545KB，
        // 于是走 `offset < mCacheStart` → reposition → 丢缓存重开连接 →
        // 新预取继续前跑 → 下次 readAt 依然落在左沿之前 → **死循环**。
        // 表现为 onRead 空转 10.2% CPU(本地同位置 2.2%)、队列恒空、
        // astarve 12~15/秒、网络播放完全出不了帧。
        //
        // 本地文件上这个前提永远不会暴露(直接 seek 文件，没有窗口概念) ——
        // 又一个双路径下判据没跟上的例子。
        //
        // 新策略：只有缓存快满时才丢最旧数据，且左沿最多推进到
        // (当前读位置 − kKeepBackBytes)，把回读窗口留出来。
        // 触发阈值取容量的 3/4 而不是 `used + keepBack >= cacheBytes`。
        // 后者等价于 used >= 28MB, 而实测素材只有 26.7MB —— 阈值**永远达不到**,
        // 左沿从此再不推进, 等于把"每读必进"改成"永不推进", 两个极端都不对。
        // 小文件整体装得下时本就不需要腾空间, 大文件到 3/4 才开始丢最旧数据。
        const size_t used = static_cast<size_t>(mCacheEnd - mCacheStart);
        if (used >= mConfig.cacheBytes / 4 * 3) {
            const int64_t keepFrom = offset - static_cast<int64_t>(kKeepBackBytes);
            if (keepFrom > mCacheStart) {
                mHead = (mHead + static_cast<size_t>(keepFrom - mCacheStart)) %
                        mConfig.cacheBytes;
                mCacheStart = keepFrom;
                mSpaceAvailable.notify_all();
            }
        }
        return static_cast<ssize_t>(toCopy);
    }

    int VEBufferedDataSource::bufferedPercent() const {
        if (mTotalSize <= 0) {
            return -1;
        }
        const double ratio = static_cast<double>(mCacheEnd) / static_cast<double>(mTotalSize);
        return static_cast<int>(ratio * 100.0);
    }

    void VEBufferedDataSource::notifyBuffering(int event, int percent) {
        if (mOnBuffering) {
            mOnBuffering(event, percent);
        }
    }

    int64_t VEBufferedDataSource::size() const {
        std::lock_guard<std::mutex> lk(mMutex);
        return mTotalSize;
    }

    void VEBufferedDataSource::abort() {
        mAbort = true;
        mRunning = false;
        if (mUpstream) {
            mUpstream->abort();
        }
        mDataAvailable.notify_all();
        mSpaceAvailable.notify_all();
    }

    void VEBufferedDataSource::close() {
        abort();
        if (mPrefetchThread.joinable()) {
            mPrefetchThread.join();
        }
        if (mUpstream) {
            mUpstream->close();
        }
    }
}
