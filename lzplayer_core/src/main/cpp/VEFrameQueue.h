#ifndef __VE_FRAME_QUEUE__
#define __VE_FRAME_QUEUE__
#include"VEFrame.h"
#include<pthread.h>
#include<queue>

class VEFrameQueue
{
public:
    VEFrameQueue(/* args */);
    ~VEFrameQueue();

    int put(VEFrame *pack);
    VEFrame* get();
    int size();

private:
    std::queue<VEFrame*> mFrameQueue;
    pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t mCond = PTHREAD_COND_INITIALIZER;
};
#endif