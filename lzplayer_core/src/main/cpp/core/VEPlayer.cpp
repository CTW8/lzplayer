#include "VEPlayer.h"

#include <utility>


VEResult VEPlayer::setDataSource(std::string path)
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetDataSource, shared_from_this());
    msg->setString("path", path);
    msg->post();
    return 0;
}

VEResult VEPlayer::prepare()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,shared_from_this());
    msg->post();
    return VE_OK;
}

VEResult VEPlayer::prepareAsync() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,shared_from_this());
    msg->post();
    return 0;
}

VEResult VEPlayer::start()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
    return 0;
}

VEResult VEPlayer::stop()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
    return 0;
}

VEResult VEPlayer::pause()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
    return 0;
}

VEResult VEPlayer::release()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::make_shared<AMessage>(kWhatRelease, shared_from_this())->post();
    return 0;
}

VEResult VEPlayer::seek(double timestampMs)
{
    ALOGI("VEPlayer::%s timestampMs:%f", __FUNCTION__,timestampMs);
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
    msg->setDouble("timestampMs", timestampMs);
    msg->post();
    return VE_OK;
}

VEResult VEPlayer::reset()
{
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
    std::make_shared<AMessage>(kWhatReset, shared_from_this())->post();
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
            ALOGI("VEPlayer::onMessageReceived - kWhatAudioDecEvent received");
            break;
        }
        case kWhatDemuxEvent:{
            ALOGI("VEPlayer::onMessageReceived - kWhatDemuxEvent received");
            break;
        }
        case kWhatVideoDecEvent:{
            ALOGI("VEPlayer::onMessageReceived - kWhatVideoDecEvent received");
            break;
        }
        case kWhatSetDataSource:{
            ALOGI("VEPlayer::onMessageReceived - kWhatSetDataSource received");
            onSetDataSource(msg);
            break;
        }
        case kWhatPrepare:{
            ALOGI("VEPlayer::onMessageReceived - kWhatPrepare received");
            onPrepare(msg);
            break;
        }
        case kWhatSetVideoSurface:{
            ALOGI("VEPlayer::onMessageReceived - kWhatSetVideoSurface received");
            ANativeWindow *window = nullptr;
            msg->findPointer("window", (void **)&window);
            int32_t width = 0;
            int32_t height = 0;
            msg->findInt32("width", &width);
            msg->findInt32("height", &height);
            if(window){
                setDisplayOut(window,width,height);
            }
            break;
        }
        case kWhatStart:{
            ALOGI("VEPlayer::onMessageReceived - kWhatStart received");
            onStart(msg);
            break;
        }
        case kWhatVideoNotify:{
            ALOGI("VEPlayer::onMessageReceived - kWhatVideoNotify received");
            break;
        }
        case kWhatAudioNotify:{
            ALOGI("VEPlayer::onMessageReceived - kWhatAudioNotify received");
            break;
        }
        case kWhatClosedCaptionNotify:{
            ALOGI("VEPlayer::onMessageReceived - kWhatClosedCaptionNotify received");
            break;
        }
        case kWhatReset:{
            ALOGI("VEPlayer::onMessageReceived - kWhatReset received");
            onReset(msg);
            break;
        }
        case kWhatSeek:{
            ALOGI("VEPlayer::onMessageReceived - kWhatSeek received");
            onSeek(msg);
            break;
        }
        case kWhatPause:{
            ALOGI("VEPlayer::onMessageReceived - kWhatPause received");
            onPause(msg);
            break;
        }
        case kWhatStop:{
            ALOGI("VEPlayer::onMessageReceived - kWhatStop received");
            onStop(msg);
            break;
        }
        case kWhatRelease:{
            ALOGI("VEPlayer::onMessageReceived - kWhatRelease received");
            onRelease(msg);
            break;
        }
        default:{
            break;
        }
    }
}

VEResult VEPlayer::onSetDataSource(std::shared_ptr<AMessage> msg) {
    std::string path;
    if (!msg->findString("path", path) || path.empty()) {
        ALOGE("VEPlayer::%s - Invalid path", __FUNCTION__);
        return VE_UNKNOWN_ERROR; // Define this error code
    }
    mPath = path;

    mDemuxLooper = std::make_shared<ALooper>();
    mDemuxLooper->setName("demux_thread");
    mDemuxLooper->start(false);

    mDemux = std::make_shared<VEDemux>();
    mDemuxLooper->registerHandler(mDemux);
    return 0;
}

VEResult VEPlayer::onPrepare(std::shared_ptr<AMessage> msg) {
    if(mDemux->open(mPath) != VE_OK){
        //notify error
        onErrorCallback(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED,"demux open failed!!");
    }

    mMediaInfo = mDemux->getFileInfo();

    mAudioDecodeLooper = std::make_shared<ALooper>();
    mAudioDecodeLooper->setName("adec_thread");
    mAudioDecodeLooper->start(false);

    mAudioDecoder = std::make_shared<VEAudioDecoder>();
    mAudioDecodeLooper->registerHandler(mAudioDecoder);
    mAudioDecoder->init(mDemux);

    mVideoDecodeLooper = std::make_shared<ALooper>();
    mVideoDecodeLooper->setName("vdec_thread");
    mVideoDecodeLooper->start(false);

    mVideoDecoder = std::make_shared<VEVideoDecoder>();
    mVideoDecodeLooper->registerHandler(mVideoDecoder);
    mVideoDecoder->init(mDemux);

    mVideoRenderLooper = std::make_shared<ALooper>();
    mVideoRenderLooper->setName("video_render");
    mVideoRenderLooper->start(false);
    std::shared_ptr<AMessage> renderNotify = std::make_shared<AMessage>(kWhatRenderEvent, shared_from_this());
    mAVSync = std::make_shared<VEAVsync>();
    mVideoRender = std::make_shared<VEVideoRender>(renderNotify, mAVSync);
    mVideoRenderLooper->registerHandler(mVideoRender);
    mVideoRender->init(mVideoDecoder, mWindow, mViewWidth, mViewHeight, mMediaInfo->fps);

    mAudioOutputLooper = std::make_shared<ALooper>();
    mAudioOutputLooper->setName("audio_render");
    mAudioOutputLooper->start(false);

    mAudioOutput = std::make_shared<AudioOpenSLESOutput>(renderNotify, mAVSync);
    mAudioOutputLooper->registerHandler(mAudioOutput);
    mAudioOutput->init(mAudioDecoder, 44100, 2, 1);

    onPreparedCallback();
    return 0;
}

VEResult VEPlayer::onStart(std::shared_ptr<AMessage> msg) {
    mDemux->start();
    mVideoDecoder->start();
    mAudioDecoder->start();
    mVideoRender->start();
    mAudioOutput->start();
    return 0;
}

VEResult VEPlayer::onStop(std::shared_ptr<AMessage> msg) {
    mDemux->stop();
    mVideoDecoder->stop();
    mAudioDecoder->stop();
    mVideoRender->stop();
    mAudioOutput->stop();
    return 0;
}

VEResult VEPlayer::onPause(std::shared_ptr<AMessage> msg) {
    mDemux->pause();
    mVideoDecoder->pause();
    mAudioDecoder->pause();
    mVideoRender->pause();
    mAudioOutput->pause();
    return 0;
}

VEResult VEPlayer::onSeek(std::shared_ptr<AMessage> msg) {
    double timestampMs;
    if (msg->findDouble("timestampMs", &timestampMs)) {
        mDemux->seek(timestampMs);
        mVideoDecoder->flush();
        mAudioDecoder->flush();
        onSeekComplateCallback();
    }
    return 0;
}

VEResult VEPlayer::onReset(std::shared_ptr<AMessage> msg) {
    mDemux->seek(0);
    mVideoDecoder->flush();
    mAudioDecoder->flush();
    return 0;
}

VEResult VEPlayer::onRelease(std::shared_ptr<AMessage> msg) {
    if (mWindow) {
        ANativeWindow_release(mWindow);
        mWindow = nullptr;
    }
    return 0;
}

VEPlayer::VEPlayer() {

}

VEPlayer::~VEPlayer() {
    ALOGI("VEPlayer::%s  enter",__FUNCTION__ );
}

VEResult VEPlayer::setDisplayOut(ANativeWindow *win,int viewWidth,int viewHeight)
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
        ALOGE("VEPlayer::%s mMediaInfo is null!!!");
        return VE_UNKNOWN_ERROR;
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

void VEPlayer::setOnCompletionListener(funOnCompletionCallback callback) {
    onCompleteCallback = std::move(callback);
}

void VEPlayer::setOnErrorListener(funOnErrorCallback callback) {
    onErrorCallback = std::move(callback);
}

void VEPlayer::setOnEOSListener(funOnEOSCallback callback) {
    onEosCallback = std::move(callback);
}

void VEPlayer::setOnPreparedListener(funOnPreparedCallback callback){
    onPreparedCallback = std::move(callback);
}

void VEPlayer::setOnSeekComplateListener(funOnSeekComplateCallback callback){
    onSeekComplateCallback = std::move(callback);
}

VEResult VEPlayer::setPlaySpeed(float speed) {

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
    if (mVideoEOS && mAudioEOS) {
        if (!mEnableLoop) {
            onCompleteCallback();
            mVideoEOS = false;
            mAudioEOS = false;
            seek(0);
            pause();
        } else {
            mVideoEOS = false;
            mAudioEOS = false;
            seek(0);
            start();
            ALOGI("VEPlayer::%s Starting loop", __FUNCTION__);
        }
    }
}

