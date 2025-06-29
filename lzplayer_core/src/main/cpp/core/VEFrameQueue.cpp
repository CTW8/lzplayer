#include "VEFrameQueue.h"
namespace VE {
    VEFrameQueue::VEFrameQueue(int maxSize) : mMaxSize(maxSize) {
        // 可以添加初始化日志或其他必要的初始化操作
    }

    bool VEFrameQueue::put(const std::shared_ptr<VEFrame> &frame) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (static_cast<int>(mFrameQueue.size()) >= mMaxSize) {
            // 队列已满
            return false;
        }
        mFrameQueue.push(frame);
        return true; // 成功放入队列
    }

    std::shared_ptr<VEFrame> VEFrameQueue::get() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mFrameQueue.empty()) {
            // 队列为空
            return nullptr;
        }
        auto frame = mFrameQueue.front();
        mFrameQueue.pop();
        return frame; // 成功获取数据
    }

    int VEFrameQueue::getRemainingSize() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mMaxSize - static_cast<int>(mFrameQueue.size());
    }

    int VEFrameQueue::getDataSize() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<int>(mFrameQueue.size());
    }

    void VEFrameQueue::clear() {
        std::lock_guard<std::mutex> lock(mMutex);
        std::queue<std::shared_ptr<VEFrame>> emptyQueue;
        std::swap(mFrameQueue, emptyQueue); // 快速清空队列
    }
}