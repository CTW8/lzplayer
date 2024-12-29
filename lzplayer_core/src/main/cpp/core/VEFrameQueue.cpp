#include "VEFrameQueue.h"

VEFrameQueue::VEFrameQueue(int maxSize) : mMaxSize(maxSize) {}

VEFrameQueue::~VEFrameQueue() {
    clear();
}

bool VEFrameQueue::put(std::shared_ptr<VEFrame> frame) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mFrameQueue.size() >= mMaxSize) {
        return false; // 队列已满，返回false
    }
    mFrameQueue.push(frame);
    return true; // 成功放入队列
}

std::shared_ptr<VEFrame> VEFrameQueue::get() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mFrameQueue.empty()) {
        return nullptr; // 队列为空，返回nullptr
    }
    std::shared_ptr<VEFrame> frame = mFrameQueue.front();
    mFrameQueue.pop();
    return frame; // 成功获取数据
}

int VEFrameQueue::getRemainingSize() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mMaxSize - mFrameQueue.size();
}

int VEFrameQueue::getDataSize() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mFrameQueue.size();
}

void VEFrameQueue::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mFrameQueue.empty()) {
        mFrameQueue.front().reset();
        mFrameQueue.pop();
    }
}
