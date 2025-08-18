#include "VEPlayer.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"

#include <utility>
namespace VE {
    VEPlayer::VEPlayer() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEPlayer::~VEPlayer() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::setDataSource(std::string path) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetDataSource,
                                                                   shared_from_this());
        msg->setString("path", path);
        msg->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::setDisplayOut(ANativeWindow *win, int viewWidth, int viewHeight) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetVideoSurface,shared_from_this());
        msg->setPointer("window",win);
        msg->setInt32("viewWidth",viewWidth);
        msg->setInt32("viewHeight",viewHeight);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::prepare() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,
                                                                   shared_from_this());

//    std::shared_ptr<AMessage> respon;
//    msg->postAndAwaitResponse(&respon);
        msg->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::prepareAsync() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,
                                                                   shared_from_this());
        msg->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::start() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::stop() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::pause() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::release() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatRelease, shared_from_this())->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::seek(double timestampMs) {
        ALOGI("VEPlayer::%s enter timestampMs:%f", __FUNCTION__, timestampMs);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestampMs", timestampMs);
        msg->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::reset() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatReset, shared_from_this())->post();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        switch (msg->what()) {
            case kWhatComponentEvent: {
                ALOGI("VEPlayer::onMessageReceived - kWhatRenderEvent received");
                int type = EComponentType::E_COMPONENT_TYPE_UNKNOW;
                msg->findInt32("type",&type);
                switch(type){
                    case EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER:{
                        onAudioDecoderEvent(msg);
                        break;
                    }
                    case EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER:{
                        onVideoRenderEvent(msg);
                        break;
                    }
                    case EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER:{
                        onAudioRenderEvent(msg);
                        break;
                    }
                    case EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER:{
                        onVideoDecoderEvent(msg);
                        break;
                    }
                    case EComponentType::E_COMPONENT_TYPE_DEMUX:{
                        onDemuxEvent(msg);
                        break;
                    }
                    default:{
                        ALOGD("can't find type:%d",type);
                        break;
                    }
                }
                break;
            }
            case kWhatSetDataSource: {
                ALOGI("VEPlayer::onMessageReceived - kWhatSetDataSource received");
                onSetDataSource(msg);
                break;
            }
            case kWhatPrepare: {
                ALOGI("VEPlayer::onMessageReceived - kWhatPrepare received");
                onPrepare(msg);
                break;
            }
            case kWhatSetVideoSurface: {
                ALOGI("VEPlayer::onMessageReceived - kWhatSetVideoSurface received");
                ANativeWindow *window = nullptr;
                msg->findPointer("window", (void **) &window);
                int32_t width = 0;
                int32_t height = 0;
                msg->findInt32("viewWidth", &width);
                msg->findInt32("viewHeight", &height);
                if (window) {
                    onSurfaceChanged(window, width, height);
                }
                break;
            }
            case kWhatStart: {
                ALOGI("VEPlayer::onMessageReceived - kWhatStart received");
                onStart(msg);
                break;
            }
            case kWhatVideoNotify: {
                ALOGI("VEPlayer::onMessageReceived - kWhatVideoNotify received");
                break;
            }
            case kWhatAudioNotify: {
                ALOGI("VEPlayer::onMessageReceived - kWhatAudioNotify received");
                break;
            }
            case kWhatClosedCaptionNotify: {
                ALOGI("VEPlayer::onMessageReceived - kWhatClosedCaptionNotify received");
                break;
            }
            case kWhatReset: {
                ALOGI("VEPlayer::onMessageReceived - kWhatReset received");
                onReset(msg);
                break;
            }
            case kWhatSeek: {
                ALOGI("VEPlayer::onMessageReceived - kWhatSeek received");
                onSeek(msg);
                break;
            }
            case kWhatPause: {
                ALOGI("VEPlayer::onMessageReceived - kWhatPause received");
                onPause(msg);
                break;
            }
            case kWhatStop: {
                ALOGI("VEPlayer::onMessageReceived - kWhatStop received");
                onStop(msg);
                break;
            }
            case kWhatRelease: {
                ALOGI("VEPlayer::onMessageReceived - kWhatRelease received");
                onRelease(msg);
                break;
            }
            default: {
                break;
            }
        }
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onSetDataSource(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        std::string path;
        if (!msg->findString("path", path) || path.empty()) {
            ALOGE("VEPlayer::%s - Invalid path", __FUNCTION__);
            return VE_UNKNOWN_ERROR; // Define this error code
        }
        mPath = path;

        mRenderNotifyMsg = std::make_shared<AMessage>(kWhatComponentEvent, shared_from_this());

        mDemuxLooper = std::make_shared<ALooper>();
        mDemuxLooper->setName("demux_thread");
        mDemuxLooper->start(false);

        mDemux = std::make_shared<VEDemux>(mRenderNotifyMsg);
        mDemuxLooper->registerHandler(mDemux);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        VEBundle params;
        params.set("path", mPath);
        if (mDemux->prepare(params) != VE_OK) {
            //notify error
            onErrorCallback(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "demux open failed!!");
        }

        mMediaInfo = mDemux->getFileInfo();
        mAVSync = std::make_shared<VEAVsync>();

        if(mMediaInfo->audio_stream_index != -1) {
            mAudioDecodeLooper = std::make_shared<ALooper>();
            mAudioDecodeLooper->setName("adec_thread");
            mAudioDecodeLooper->start(false);

            mAudioDecoder = std::make_shared<VEAudioDecoder>(mRenderNotifyMsg);
            mAudioDecodeLooper->registerHandler(mAudioDecoder);
            mAudioDecoder->prepare(mDemux);

            mAudioOutputLooper = std::make_shared<ALooper>();
            mAudioOutputLooper->setName("audio_render");
            mAudioOutputLooper->start(false);

            mAudioOutput = std::make_shared<VEAudioRender>(mRenderNotifyMsg, mAVSync);
            mAudioOutputLooper->registerHandler(mAudioOutput);
            VEBundle params;
            params.set("samplerate",44100);
            params.set("channel",2);
            params.set("format",1);
            params.set("decode",mAudioDecoder);
            mAudioOutput->prepare(params);
        }

        if(mMediaInfo->video_stream_index != -1) {
            mVideoDecodeLooper = std::make_shared<ALooper>();
            mVideoDecodeLooper->setName("vdec_thread");
            mVideoDecodeLooper->start(false);

            mVideoDecoder = std::make_shared<VEVideoDecoder>(mRenderNotifyMsg);
            mVideoDecodeLooper->registerHandler(mVideoDecoder);
            mVideoDecoder->prepare(mDemux);

            mVideoRenderLooper = std::make_shared<ALooper>();
            mVideoRenderLooper->setName("video_render");
            mVideoRenderLooper->start(false);
            mVideoRender = std::make_shared<VEVideoDisplay>(mRenderNotifyMsg, mAVSync);
            mVideoRenderLooper->registerHandler(mVideoRender);

//            mVideoRender->prepare(mVideoDecoder, mWindow, mViewWidth, mViewHeight, mMediaInfo->fps);

            VEBundle params;
            params.set("surface",mWindow);
            params.set("width",mViewWidth);
            params.set("height",mViewHeight);
            params.set("fps",mMediaInfo->fps);
            params.set("decoder",mVideoDecoder);
            mVideoRender->prepare(params);
            // 如果Surface已经设置，则在初始化后调用setSurface
//            if (mWindow != nullptr) {
//                mVideoRender->setSurface(mWindow, mViewWidth, mViewHeight);
//            }
        }

        onPreparedCallback();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onStart(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        mVideoRender->start();
        mAudioOutput->start();

        mVideoDecoder->start();
        mAudioDecoder->start();

        mDemux->start();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onStop(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        mVideoRender->stop();
        mAudioOutput->stop();

        mVideoDecoder->stop();
        mAudioDecoder->stop();

        mDemux->stop();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onPause(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        mVideoRender->pause();
        mAudioOutput->pause();
        mVideoDecoder->pause();
        mAudioDecoder->pause();
        mDemux->pause();
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onSeek(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        double timestampMs;
        if (msg->findDouble("timestampMs", &timestampMs)) {
            mVideoRender->seekTo(timestampMs);
            mAudioOutput->seekTo(timestampMs);
            mVideoDecoder->seekTo(timestampMs);
            mAudioDecoder->seekTo(timestampMs);
            mDemux->seekTo(timestampMs);
            //从这里发出时不对的，应该在精准seek解码后渲染完成后发出
            onSeekComplateCallback();
        }
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onReset(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        mVideoDecoder->flush();
        mAudioDecoder->flush();
        mDemux->seekTo(0);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onRelease(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        if (mWindow) {
            ANativeWindow_release(mWindow);
            mWindow = nullptr;
        }
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    void VEPlayer::setLooping(bool enable) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        mEnableLoop = enable;
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    long VEPlayer::getCurrentPosition() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    long VEPlayer::getDuration() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        if (mMediaInfo == nullptr) {
            ALOGE("VEPlayer mMediaInfo is null!!!");
            return VE_UNKNOWN_ERROR;
        }
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return mMediaInfo->duration;
    }

    void VEPlayer::setVolume(int volume) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnInfoListener(funOnInfoCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onInfoCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnProgressListener(funOnProgressCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onProgressCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnCompletionListener(funOnCompletionCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onCompleteCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnErrorListener(funOnErrorCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onErrorCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnEOSListener(funOnEOSCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onEosCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnPreparedListener(funOnPreparedCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onPreparedCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnSeekComplateListener(funOnSeekComplateCallback callback) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        onSeekComplateCallback = std::move(callback);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::setPlaySpeed(float speed) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    void VEPlayer::onEOS() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        if (mVideoEOS && mAudioEOS) {
            ALOGI("VEPlayer::%s play complate", __FUNCTION__);
            onCompleteCallback();
            mVideoEOS = false;
            mAudioEOS = false;
        }
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onSurfaceChanged(ANativeWindow *win, int viewWidth, int viewHeight) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        mWindow = win;
        mViewWidth = viewWidth;
        mViewHeight = viewHeight;

        // 只有在mVideoRender已经初始化后才调用setSurface
        if (mVideoRender != nullptr) {
            mVideoRender->setSurface(mWindow, mViewWidth, mViewHeight);
        }

        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onVideoRenderEvent(std::shared_ptr<AMessage> msg) {
        int eventType = 0;
        msg->findInt32("event",&eventType);
        switch (eventType) {
            case VE_NOTIFY_EVENT_SEEK_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DOING:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_OPEN_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_STOP_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_EOS:{
                mVideoEOS = true;
                onEOS();
                break;
            }
            case VE_NOTIFY_EVENT_PROGRESS:{
                int64_t progress=0;
                msg->findInt64("arg3",&progress);
                notifyProgress(progress);
                break;
            }
            default:{
                ALOGD("event type:%d",eventType);
                break;
            }
        }
        return 0;
    }

    VEResult VEPlayer::onAudioRenderEvent(std::shared_ptr<AMessage> msg) {
        int eventType = 0;
        msg->findInt32("event",&eventType);
        switch (eventType) {
            case VE_NOTIFY_EVENT_SEEK_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DOING:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_OPEN_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_STOP_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_EOS:{
                mAudioEOS = true;
                onEOS();
                break;
            }
            default:{
                ALOGD("event type:%d",eventType);
                break;
            }
        }
        return 0;
    }

    VEResult VEPlayer::onVideoDecoderEvent(std::shared_ptr<AMessage> msg) {
        int eventType = 0;
        msg->findInt32("event",&eventType);
        switch (eventType) {
            case VE_NOTIFY_EVENT_SEEK_DONE:{

                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DOING:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_OPEN_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_STOP_DONE:{
                break;
            }
            default:{
                ALOGD("event type:%d",eventType);
                break;
            }
        }
        return 0;
    }

    VEResult VEPlayer::onAudioDecoderEvent(std::shared_ptr<AMessage> msg) {
        int eventType = 0;
        msg->findInt32("event",&eventType);
        switch (eventType) {
            case VE_NOTIFY_EVENT_SEEK_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DOING:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_OPEN_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_STOP_DONE:{
                break;
            }
        }
        return 0;
    }

    VEResult VEPlayer::onDemuxEvent(std::shared_ptr<AMessage> msg) {
        int eventType = 0;
        msg->findInt32("event",&eventType);
        switch (eventType) {
            case VE_NOTIFY_EVENT_SEEK_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DOING:{
                break;
            }
            case VE_NOTIFY_EVENT_FLUSH_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_OPEN_DONE:{
                break;
            }
            case VE_NOTIFY_EVENT_STOP_DONE:{
                break;
            }
        }
        return 0;
    }
}