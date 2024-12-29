#include"VEPacketQueue.h"

VEPacketQueue::VEPacketQueue(int maxSize) : mMaxSize(maxSize)
{
}

VEPacketQueue::~VEPacketQueue()
{
    clear(); // 确保析构时清空队列
}

bool VEPacketQueue::put(std::shared_ptr<VEPacket> pack)
{
    pthread_mutex_lock(&mMutex);
    if(mPacketQueue.size() >= mMaxSize){
        pthread_mutex_unlock(&mMutex);
        return false; // 队列已满，返回false
    }
    mPacketQueue.push(pack);
    pthread_mutex_unlock(&mMutex);
    return true; // 成功放入队列
}

std::shared_ptr<VEPacket> VEPacketQueue::get()
{
    pthread_mutex_lock(&mMutex);
    if(mPacketQueue.empty()){
        pthread_mutex_unlock(&mMutex);
        return nullptr; // 队列为空，返回nullptr
    }
    std::shared_ptr<VEPacket> tmp = mPacketQueue.front();
    mPacketQueue.pop();
    pthread_mutex_unlock(&mMutex);
    return tmp; // 成功获取数据
}

int VEPacketQueue::getRemainingSize()
{
    pthread_mutex_lock(&mMutex);
    int remainingSize = mMaxSize - mPacketQueue.size();
    pthread_mutex_unlock(&mMutex);
    return remainingSize; // 返回队列剩余空间
}

int VEPacketQueue::getDataSize()
{
    pthread_mutex_lock(&mMutex);
    int dataSize = mPacketQueue.size();
    pthread_mutex_unlock(&mMutex);
    return dataSize; // 返回队列中数据的大小
}

void VEPacketQueue::clear()
{
    pthread_mutex_lock(&mMutex);
    while (!mPacketQueue.empty()) {
        mPacketQueue.front().reset();
        mPacketQueue.pop();
    }
    pthread_mutex_unlock(&mMutex);
}
