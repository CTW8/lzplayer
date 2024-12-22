#include"VEFrameQueue.h"

VEFrameQueue::VEFrameQueue()
{
}

VEFrameQueue::~VEFrameQueue()
{
}

int VEFrameQueue::put(VEFrame *pack)
{
    ///判断是否size到达阈值，到达后wait
    if(mFrameQueue.size()>4){
        pthread_mutex_lock(&mMutex);
        pthread_cond_wait(&mCond,&mMutex);
        pthread_mutex_unlock(&mMutex);
    }
    ///放入队列，并发送信号
    pthread_mutex_lock(&mMutex);
    mFrameQueue.push(pack);
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
    return 0;
}

VEFrame *VEFrameQueue::get()
{
    ///判断是否队列为空，如果是空的则wait
    if(mFrameQueue.empty()){
        pthread_mutex_lock(&mMutex);
        pthread_cond_wait(&mCond,&mMutex);
        pthread_mutex_unlock(&mMutex);
    }
    ///有数据则获取，并发送信号，通知有空间可以存放，
    pthread_mutex_lock(&mMutex);
    VEFrame *tmp = mFrameQueue.front();
    mFrameQueue.pop();
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
    return tmp;
}

int VEFrameQueue::size()
{
    return 0;
}
