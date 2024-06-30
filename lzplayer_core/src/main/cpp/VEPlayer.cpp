#include "VEPlayer.h"


int VEPlayer::setDataSource(std::string path)
{
    if(path.empty()){
        printf("## %s %d  params is invalid!!\n",__FUNCTION__,__LINE__);
        return VE_INVALID_PARAMS;
    }
    mPath = path;
    return 0;
}

int VEPlayer::prepare()
{
    ///创建demux线程
    mDemuxLooper = std::make_shared<ALooper>();
    mDemuxLooper->setName("demux_thread");
    mDemuxLooper->start(false);

    mDemux = std::shared_ptr<VEDemux>();

    mDemuxLooper->registerHandler(mDemux);
    mDemux->open(mPath);

    mMediaInfo = mDemux->getFileInfo();

    ///创建audio dec thread
    mAudioDecodeLooper = std::make_shared<ALooper>();
    mAudioDecodeLooper->setName("adec_thread");
    mAudioDecodeLooper->start(false);

    mAudioDecoder = std::shared_ptr<VEAudioDecoder>();
    mAudioDecodeLooper->registerHandler(mAudioDecoder);

    mAudioDecoder->init(mDemux);

    ///创建video dec thread

    mVideoDecodeLooper = std::make_shared<ALooper>();
    mVideoDecodeLooper->setName("vdec_thread");
    mVideoDecodeLooper->start(false);

    mVideoDecoder = std::make_shared<VEVideoDecoder>();

    mVideoDecodeLooper->registerHandler(mVideoDecoder);
    mVideoDecoder->init(mDemux);


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

    if(mWindow){
        ANativeWindow_release(mWindow);
    }
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

void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {

}

VEPlayer::VEPlayer() {

}

VEPlayer::~VEPlayer() {

}

int VEPlayer::setDisplayOut(ANativeWindow *win) {

    mWindow = win;
    return 0;
}
