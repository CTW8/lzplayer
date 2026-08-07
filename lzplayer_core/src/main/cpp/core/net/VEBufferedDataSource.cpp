#include "VEBufferedDataSource.h"

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
                    mSpaceAvailable.wait_for(lk, std::chrono::milliseconds(kWaitSliceMs));
                }
                if (!mRunning || mAbort || mUpstreamEof) {
                    break;
                }
                space = mConfig.cacheBytes - static_cast<size_t>(mCacheEnd - mCacheStart);
                fetchAt = mCacheEnd;
            }

            const size_t want = std::min(space, chunk.size());
            const ssize_t got = mUpstream->readAt(fetchAt, chunk.data(), want);

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
                ALOGI("VEBufferedDataSource::%s upstream EOF at %lld", __FUNCTION__,
                      static_cast<long long>(fetchAt));
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
        const VEResult ret = mUpstream->open(mUrl, offset);
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
        mPrefetchThread = std::thread(&VEBufferedDataSource::prefetchLoop, this);
        return VE_OK;
    }

    ssize_t VEBufferedDataSource::readAt(int64_t offset, void *buf, size_t size) {
        std::unique_lock<std::mutex> lk(mMutex);

        if (offset < mCacheStart ||
            offset > mCacheEnd + mConfig.forwardSkipMax) {
            // 后向 seek，或前向跨得太远等不起 → 重定位
            if (reposition(lk, offset) != VE_OK) {
                return -1;
            }
        }

        // 等预取把这一段填上来
        while (availableFromLocked(offset) == 0) {
            if (mAbort) {
                return -1;
            }
            if (mPrefetchError != VE_OK) {
                return -1;
            }
            if (mUpstreamEof) {
                return 0;   // 真的没有更多数据了
            }
            if (!mBuffering) {
                // 消费者要的数据还没到 = 卡顿。上报后上层会暂停数据面。
                mBuffering = true;
                lk.unlock();
                notifyBuffering(VE_NOTIFY_EVENT_BUFFERING_START, bufferedPercent());
                lk.lock();
            }
            mDataAvailable.wait_for(lk, std::chrono::milliseconds(kWaitSliceMs));
        }

        // 卡顿恢复要等回到恢复水位，避免在低水位反复抖动
        if (mBuffering) {
            const size_t avail = availableFromLocked(offset);
            if (avail >= mConfig.resumeWaterBytes || mUpstreamEof) {
                mBuffering = false;
                lk.unlock();
                notifyBuffering(VE_NOTIFY_EVENT_BUFFERING_END, bufferedPercent());
                lk.lock();
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

        // 消费过的部分让出空间：窗口左沿前移到本次读取的末尾。
        // demux 不会回头读已消费的数据(真要回头就是 seek，走重定位)。
        const int64_t newStart = offset + static_cast<int64_t>(toCopy);
        if (newStart > mCacheStart) {
            mHead = (mHead + static_cast<size_t>(newStart - mCacheStart)) % mConfig.cacheBytes;
            mCacheStart = newStart;
            mSpaceAvailable.notify_all();
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
