#ifndef __VE_PACKET_QUEUE__
#define __VE_PACKET_QUEUE__
#include<pthread.h>
#include"VEPacket.h"
#include<iostream>
#include <queue>

class VEPacketQueue
{
public:
    VEPacketQueue(int maxSize); // 增加构造函数参数，设置队列大小
    ~VEPacketQueue();

    bool put(std::shared_ptr<VEPacket> pack); // 修改返回类型为bool，表示是否成功放入队列
    std::shared_ptr<VEPacket> get();
    int getRemainingSize(); // 增加检查队列剩余空间的接口
    int getDataSize(); // 增加检查队列有多少数据的接口
    void clear(); // 提供清空队列的接口

private:
    std::queue<std::shared_ptr<VEPacket>> mPacketQueue;
    pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;
    int mMaxSize; // 队列最大大小
};

#endif