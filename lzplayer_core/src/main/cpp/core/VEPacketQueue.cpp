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

    void VEPacketQueue::clear() {
        std::lock_guard<std::mutex> lock(mMutex); // RAII 锁
        std::queue<std::shared_ptr<VEPacket>> emptyQueue;
        std::swap(mPacketQueue, emptyQueue); // 快速清空队列
    }
}