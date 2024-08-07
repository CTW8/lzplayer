#include "VEPlayer.h"


int VEPlayer::setDataSource(std::string path)
{
    if(path.empty()){
        printf("## %s %d  params is invalid!!\n",__FUNCTION__,__LINE__);
        return VE_INVALID_PARAMS;
    }
    mPath = path;

    ///创建demux线程
    mDemuxLooper = std::make_shared<ALooper>();
    mDemuxLooper->setName("demux_thread");
    mDemuxLooper->start(false);

    mDemux = std::make_shared<VEDemux>();

    mDemuxLooper->registerHandler(mDemux);
    mDemux->open(mPath);

    mMediaInfo = mDemux->getFileInfo();
    return 0;
}

int VEPlayer::prepare()
{
    ///创建audio dec thread
    mAudioDecodeLooper = std::make_shared<ALooper>();
    mAudioDecodeLooper->setName("adec_thread");
    mAudioDecodeLooper->start(false);

    mAudioDecoder = std::make_shared<VEAudioDecoder>();
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

    mVideoRenderLooper = std::make_shared<ALooper>();
    mVideoRenderLooper->setName("video_render");
    mVideoRenderLooper->start(false);

    mVideoRender = std::make_shared<VEVideoRender>();
    mVideoRenderLooper->registerHandler(mVideoRender);

    mVideoRender->init(mVideoDecoder,mWindow,mViewWidth,mViewHeight,mMediaInfo->fps,this);

    //创建音频播放线程
    mAudioOutputLooper = std::make_shared<ALooper>();
    mAudioOutputLooper->setName("audio_render");
    mAudioOutputLooper->start(false);

    mAudioOutput = std::make_shared<AudioOpenSLESOutput>();
    mAudioOutputLooper->registerHandler(mAudioOutput);
    mAudioOutput->init(mAudioDecoder,44100,2,1);

    return 0;
}

int VEPlayer::start()
{
    ///控制各个线程开始运行

    mDemux->start();
    mVideoDecoder->start();
    mAudioDecoder->start();
    mVideoRender->start();
    mAudioOutput->start();
    return 0;
}

int VEPlayer::stop()
{
    //控制各个线程执行状态
    mDemux->stop();
    mVideoDecoder->stop();
    mAudioDecoder->stop();
    mVideoRender->stop();
    mAudioOutput->stop();
    return 0;
}

int VEPlayer::pause()
{
    //控制各个线程执行状态
    mVideoRender->pause();
    mAudioOutput->pause();
    return 0;
}

int VEPlayer::resume()
{
    //控制各个线程执行状态
    mVideoRender->resume();
    mAudioOutput->start();
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
    mDemux->seek(timestamp);
    mVideoDecoder->flush();
    mAudioDecoder->flush();
    return 0;
}

int VEPlayer::reset()
{
    mDemux->seek(0);
    mVideoDecoder->flush();
    mAudioDecoder->flush();
    return 0;
}

//void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
//
//}

VEPlayer::VEPlayer() {

}

VEPlayer::~VEPlayer() {

}

int VEPlayer::setDisplayOut(ANativeWindow *win,int viewWidth,int viewHeight) {

    mWindow = win;
    mViewWidth = viewWidth;
    mViewHeight = viewHeight;
    return 0;
}

void VEPlayer::setLooping() {

}

long VEPlayer::getCurrentPosition() {
    return 0;
}

long VEPlayer::getDuration() {
    if(mMediaInfo == nullptr){
        return -1;
    }
    return mMediaInfo->duration;
}

void VEPlayer::setVolume(int volume) {

}

void VEPlayer::setOnInfoListener(funOnInfoCallback callback) {
    onInfoCallback = callback;
}

void VEPlayer::setOnProgressListener(funOnProgressCallback callback) {
    onProgressCallback = callback;
}
