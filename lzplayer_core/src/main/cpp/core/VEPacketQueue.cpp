#include"VEPacketQueue.h"

VEPacketQueue::VEPacketQueue()
{
}

VEPacketQueue::~VEPacketQueue()
{
}

int VEPacketQueue::put(VEPacket *pack)
{
    ///如果queue size大于指定值，先wait
    if(mPacketQueue.size()>20){
        pthread_cond_wait(&mCond,&mMutex);
    }
    ///有signal过来，有空间了，继续put
    pthread_mutex_lock(&mMutex);
    mPacketQueue.push(pack);
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
    return 0;
}

VEPacket *VEPacketQueue::get()
{
    
    ///如果没有数据了，发完signal后wait
    if(mPacketQueue.empty()){
        pthread_mutex_lock(&mMutex);
        pthread_cond_wait(&mCond,&mMutex);
        pthread_mutex_unlock(&mMutex);
    }
    ///如果有signal过来则，继续获取
    pthread_mutex_lock(&mMutex);
    pthread_cond_signal(&mCond);
    VEPacket *tmp = (VEPacket *)mPacketQueue.front();
    mPacketQueue.pop();
    pthread_mutex_unlock(&mMutex);
    return tmp;
}

int VEPacketQueue::size()
{
    return mPacketQueue.size();
}
