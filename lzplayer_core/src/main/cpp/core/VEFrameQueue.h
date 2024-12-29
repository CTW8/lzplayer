#ifndef __VE_FRAME_QUEUE__
#define __VE_FRAME_QUEUE__

#include "VEFrame.h"
#include <queue>
#include <mutex>

class VEFrameQueue {
public:
    VEFrameQueue(int maxSize);
    ~VEFrameQueue();

    bool put(std::shared_ptr<VEFrame> frame); // 返回是否成功放入队列
    std::shared_ptr<VEFrame> get();
    int getRemainingSize(); // 获取队列剩余空间
    int getDataSize(); // 获取队列中数据的数量
    void clear(); // 清空队列

private:
    std::queue<std::shared_ptr<VEFrame>> mFrameQueue;
    std::mutex mMutex;
    int mMaxSize;
};

#endif