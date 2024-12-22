#include "VEPlayer.h"

#include <utility>


int VEPlayer::setDataSource(std::string path)
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
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
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
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

    std::shared_ptr<AMessage> renderNotify = std::make_shared<AMessage>(kWhatRenderEvent,shared_from_this());
    mAVSync = std::make_shared<VEAVsync>();
    mVideoRender = std::make_shared<VEVideoRender>(renderNotify,mAVSync);
    mVideoRenderLooper->registerHandler(mVideoRender);

    mVideoRender->init(mVideoDecoder,mWindow,mViewWidth,mViewHeight,mMediaInfo->fps);

    //创建音频播放线程
    mAudioOutputLooper = std::make_shared<ALooper>();
    mAudioOutputLooper->setName("audio_render");
    mAudioOutputLooper->start(false);

    mAudioOutput = std::make_shared<AudioOpenSLESOutput>(renderNotify,mAVSync);
    mAudioOutputLooper->registerHandler(mAudioOutput);
    mAudioOutput->init(mAudioDecoder,44100,2,1);

    return 0;
}

int VEPlayer::start()
{
    ///控制各个线程开始运行
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    mDemux->start();
    mVideoDecoder->start();
    mAudioDecoder->start();
    mVideoRender->start();
    mAudioOutput->start();
    return 0;
}

int VEPlayer::stop()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
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
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    //控制各个线程执行状态
    mVideoRender->pause();
    mAudioOutput->pause();
    return 0;
}

int VEPlayer::resume()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    //控制各个线程执行状态
    mVideoRender->resume();
    mAudioOutput->resume();
    return 0;
}

int VEPlayer::release()
{
    ////释放播放器
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    if(mWindow){
        ANativeWindow_release(mWindow);
    }
    return 0;
}

int VEPlayer::seek(double timestampMs)
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    ///发送seek命令
    mDemux->seek(timestampMs);
    mVideoDecoder->flush();
    mAudioDecoder->flush();
    return 0;
}

int VEPlayer::reset()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    mDemux->seek(0);
    mVideoDecoder->flush();
    mAudioDecoder->flush();
    return 0;
}

void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatRenderEvent:{
            ALOGI("VEPlayer::onMessageReceived - kWhatRenderEvent received");
            onRenderNotify(msg);
            break;
        }
        case kWhatAudioDecEvent:{
            break;
        }
        case kWhatDemuxEvent:{
            break;
        }
        case kWhatVideoDecEvent:{
            break;
        }
        default:{
            break;
        }
    }
}

VEPlayer::VEPlayer() {

}

VEPlayer::~VEPlayer() {
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
}

int VEPlayer::setDisplayOut(ANativeWindow *win,int viewWidth,int viewHeight)
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    mWindow = win;
    mViewWidth = viewWidth;
    mViewHeight = viewHeight;
    return 0;
}

void VEPlayer::setLooping(bool enable) {
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    mEnableLoop = enable;
}

long VEPlayer::getCurrentPosition() {
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    return 0;
}

long VEPlayer::getDuration() {
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    if(mMediaInfo == nullptr){
        return -1;
    }
    return mMediaInfo->duration;
}

void VEPlayer::setVolume(int volume) {

}

void VEPlayer::setOnInfoListener(funOnInfoCallback callback) {
    onInfoCallback = std::move(callback);
}

void VEPlayer::setOnProgressListener(funOnProgressCallback callback) {
    onProgressCallback = std::move(callback);
}

int VEPlayer::setPlaySpeed(float speed) {

    return 0;
}

void VEPlayer::onRenderNotify(std::shared_ptr<AMessage> msg) {
    int32_t what=0;
    msg->findInt32("type",&what);
    switch (what) {
        case VEVideoRender::kWhatEOS:{
            ALOGI("VEPlayer::%s msg->kWhatEOS",__FUNCTION__ );
            mVideoEOS = true;
            onEOS();
            break;
        }
        case AudioOpenSLESOutput::kWhatEOS:{
            mAudioEOS = true;
            onEOS();
            break;
        }
        case VEVideoRender::kWhatProgress:{
            int64_t value;
            ALOGI("VEPlayer::%s progress: %" PRId64, __FUNCTION__, value);
            msg->findInt64("progress",&value);
            notifyProgress(value);
            break;
        }
        default:{
            break;
        }
    }
}


void VEPlayer::onEOS() {
    if(mVideoEOS && mAudioEOS){
        if(!mEnableLoop){
            notifyInfo(VE_PLAYER_NOTIFY_EVENT_ON_EOS,1,0.f, "", nullptr);
            mVideoEOS = false;
            mAudioEOS = false;
            seek(0);
            pause();
        }else{
            mVideoEOS = false;
            mAudioEOS = false;
            seek(0);
            start();
            ALOGI("VEPlayer::%s Starting loop", __FUNCTION__);
        }
    }
}

