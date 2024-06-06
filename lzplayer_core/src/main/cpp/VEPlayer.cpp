#include "VEPlayer.h"


int VEPlayer::setDataSource(std::string path)
{
    pthread_mutex_lock(&mMutex);
    if(path.empty()){
        printf("## %s %d  params is invalid!!\n",__FUNCTION__,__LINE__);
        return VE_INVALID_PARAMS;
    }
    mDemux = new VEDemux();
    mDemux->open(path);
    mMediaInfo = mDemux->getFileInfo();

    mAudioDecoder = new VEAudioDecoder();
    mAudioDecoder->init(mMediaInfo.get());

    mVideoDecoder = new VEVideoDecoder();
    mVideoDecoder->init(mMediaInfo.get());

    mAPacketQueue = new VEPacketQueue();
    mVPacketQueue = new VEPacketQueue();

    mAFrameQueue = new VEFrameQueue();
    mVFrameQueue = new VEFrameQueue();
    pthread_mutex_unlock(&mMutex);
    return 0;
}

int VEPlayer::prepare()
{
    ///创建demux线程
    

    ///创建audio dec thread


    ///创建video dec thread


    ///创建视频渲染线程


    return 0;
}

int VEPlayer::start()
{
    ///控制各个线程开始运行
    return 0;
}

int VEPlayer::stop()
{
    //控制各个线程执行状态
    return 0;
}

int VEPlayer::pause()
{
    //控制各个线程执行状态
    return 0;
}

int VEPlayer::resume()
{
    //控制各个线程执行状态
    return 0;
}

int VEPlayer::release()
{
    ////释放播放器
    return 0;
}

int VEPlayer::seek(int64_t timestamp)
{
    ///发送seek命令
    return 0;
}

int VEPlayer::reset()
{
    return 0;
}
