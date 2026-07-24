#include "VEPacketQueue.h"
namespace VE {
    VEPacketQueue::VEPacketQueue(int maxSize) : mMaxSize(maxSize) {
        // 构造函数初始化列表已初始化 mMaxSize
    }

    bool VEPacketQueue::put(const std::shared_ptr<VEPacket> &pack) {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        if (mPacketQueue.size() >= static_cast<size_t>(mMaxSize)) {
            // 队列已满
            return false;
        }
        mPacketQueue.push(pack);
        // 字节/时长记账：与 mPacketQueue 同步维护，必须在同一把锁内
        if (pack) {
            if (pack->getPacket() != nullptr && pack->getPacket()->size > 0) {
                mTotalBytes += static_cast<size_t>(pack->getPacket()->size);
            }
            if (pack->getDurationUs() > 0) {
                mTotalDurationUs += pack->getDurationUs();
            }
        }
        return true; // 成功放入队列
    }

    std::shared_ptr<VEPacket> VEPacketQueue::get() {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        if (mPacketQueue.empty()) {
            // 队列为空
            return nullptr;
        }
        auto packet = mPacketQueue.front();
        mPacketQueue.pop();
        // 与 put 对称扣减，钳到 0 防御任何意外的不配对
        if (packet) {
            if (packet->getPacket() != nullptr && packet->getPacket()->size > 0) {
                size_t bytes = static_cast<size_t>(packet->getPacket()->size);
                mTotalBytes = (mTotalBytes > bytes) ? (mTotalBytes - bytes) : 0;
            }
            if (packet->getDurationUs() > 0) {
                mTotalDurationUs = (mTotalDurationUs > packet->getDurationUs())
                                   ? (mTotalDurationUs - packet->getDurationUs()) : 0;
            }
        }
        return packet; // 成功获取数据
    }

    int VEPacketQueue::getRemainingSize() const {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        return mMaxSize - static_cast<int>(mPacketQueue.size());
    }

    int VEPacketQueue::getDataSize() const {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        return static_cast<int>(mPacketQueue.size());
    }

    size_t VEPacketQueue::getTotalBytes() const {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        return mTotalBytes;
    }

    int64_t VEPacketQueue::getDurationUs() const {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        return mTotalDurationUs;
    }

    void VEPacketQueue::clear() {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        std::queue<std::shared_ptr<VEPacket>> emptyQueue;
        std::swap(mPacketQueue, emptyQueue); // 快速清空队列
        mTotalBytes = 0;
        mTotalDurationUs = 0;
    }
}