#ifndef __VE_PACKET_QUEUE__
#define __VE_PACKET_QUEUE__
#include<pthread.h>
#include"VEPacket.h"
#include<iostream>
#include <queue>

class VEPacketQueue
{
public:
    VEPacketQueue(/* args */);
    ~VEPacketQueue();

    int put(VEPacket *pack);
    VEPacket* get();
    int size();

private:
    std::queue<VEPacket*> mPacketQueue;
    pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t mCond = PTHREAD_COND_INITIALIZER;
};

#endif