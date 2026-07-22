#include "VEPlayer.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"

#include <utility>
namespace VE {
    namespace {
        /// 单个流程阶段等待组件回执的上限，超时后强制推进，
        /// 宁可状态略有偏差也不能让 seek 永久卡死
        constexpr int64_t kAckTimeoutUs = 2000000;

        /// 进度上报间隔。原先每渲染一帧上报一次(30fps 即每秒 30 条跨线程消息
        /// 加 30 次 JNI 回调)，且纯音频文件因为没有视频渲染完全收不到进度。
        constexpr int64_t kProgressIntervalUs = 500000;
    }

    VEPlayer::VEPlayer() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    VEPlayer::~VEPlayer() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::setDataSource(std::string path) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetDataSource,
                                                                   shared_from_this());
        msg->setString("path", path);
        msg->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::setDisplayOut(ANativeWindow *win, int viewWidth, int viewHeight) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetVideoSurface,shared_from_this());
        msg->setPointer("window",win);
        msg->setInt32("viewWidth",viewWidth);
        msg->setInt32("viewHeight",viewHeight);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::prepare() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,
                                                                   shared_from_this());

//    std::shared_ptr<AMessage> respon;
//    msg->postAndAwaitResponse(&respon);
        msg->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::prepareAsync() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,
                                                                   shared_from_this());
        msg->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::start() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::stop() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::pause() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::release() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        // 同步等待：返回后各组件线程都已退出、资源已释放，调用方才能安全销毁播放器
        auto msg = std::make_shared<AMessage>(kWhatRelease, shared_from_this());
        std::shared_ptr<AMessage> response;
        if (msg->postAndAwaitResponse(&response) != OK) {
            ALOGW("VEPlayer::%s post release failed, looper may be gone", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::seek(double timestampMs) {
        ALOGV("VEPlayer::%s enter timestampMs:%f", __FUNCTION__, timestampMs);
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestampMs", timestampMs);
        msg->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::reset() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::make_shared<AMessage>(kWhatReset, shared_from_this())->post();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        switch (msg->what()) {
            case kWhatComponentEvent: {
                onComponentEvent(msg);
                break;
            }
            case kWhatAckTimeout: {
                onAckTimeout(msg);
                break;
            }
            case kWhatProgressTick: {
                onProgressTick(msg);
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
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onSetDataSource(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::string path;
        if (!msg->findString("path", path) || path.empty()) {
            ALOGE("VEPlayer::%s - Invalid path", __FUNCTION__);
            return VE_UNKNOWN_ERROR; // Define this error code
        }
        // 允许不经 reset 直接换片源：先拆掉上一套组件，否则会再建一套 looper
        if (mDemux != nullptr) {
            ALOGI("VEPlayer::%s tear down previous pipeline first", __FUNCTION__);
            teardownComponents();
        }

        mPath = path;
        mState = STATE_IDLE;

        mRenderNotifyMsg = std::make_shared<AMessage>(kWhatComponentEvent, shared_from_this());

        mDemuxLooper = std::make_shared<ALooper>();
        mDemuxLooper->setName("demux_thread");
        mDemuxLooper->start(false);

        mDemux = std::make_shared<VEDemux>(mRenderNotifyMsg);
        mDemuxLooper->registerHandler(mDemux);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        VEBundle params;
        params.set("path", mPath);
        if (mDemux->prepare(params) != VE_OK) {
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "demux open failed!!");
            return VE_UNKNOWN_ERROR;
        }

        mMediaInfo = mDemux->getFileInfo();
        if (mMediaInfo == nullptr ||
            (mMediaInfo->audio_stream_index == -1 && mMediaInfo->video_stream_index == -1)) {
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "no playable stream found!!");
            return VE_UNKNOWN_ERROR;
        }
        mMediaClock = std::make_shared<VEMediaClock>();
        mAVSync = std::make_shared<VEAVsync>(mMediaClock);
        if (mMediaInfo->fps > 0) {
            mAVSync->setFrameRate(mMediaInfo->fps);
        }

        if(mMediaInfo->audio_stream_index != -1) {
            mAudioDecodeLooper = std::make_shared<ALooper>();
            mAudioDecodeLooper->setName("adec_thread");
            mAudioDecodeLooper->start(false);

            // 输出参数只在这里算一次，解码器和渲染器共用，避免两处各写死一份
            const VEAudioOutputConfig audioOut =
                    chooseAudioOutputConfig(mMediaInfo->sampleRate, mMediaInfo->channels);
            ALOGI("VEPlayer::%s audio src %dHz %dch -> out %dHz %dch", __FUNCTION__,
                  mMediaInfo->sampleRate, mMediaInfo->channels,
                  audioOut.sampleRate, audioOut.channels);

            mAudioDecoder = std::make_shared<VEAudioDecoder>(mRenderNotifyMsg);
            mAudioDecodeLooper->registerHandler(mAudioDecoder);
            mAudioDecoder->prepare(mDemux, audioOut);

            mAudioOutputLooper = std::make_shared<ALooper>();
            mAudioOutputLooper->setName("audio_render");
            mAudioOutputLooper->start(false);

            mAudioOutput = std::make_shared<VEAudioRender>(mRenderNotifyMsg, mAVSync);
            mAudioOutputLooper->registerHandler(mAudioOutput);
            VEBundle params;
            params.set("samplerate", audioOut.sampleRate);
            params.set("channel", audioOut.channels);
            params.set("format", audioOut.format);
            params.set("decode", std::static_pointer_cast<IMediaDecoder>(mAudioDecoder));
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
            params.set("decoder", std::static_pointer_cast<IMediaDecoder>(mVideoDecoder));
            mVideoRender->prepare(params);
            // 如果Surface已经设置，则在初始化后调用setSurface
//            if (mWindow != nullptr) {
//                mVideoRender->setSurface(mWindow, mViewWidth, mViewHeight);
//            }
        }

        mState = STATE_PREPARED;
        if (onPreparedCallback) {
            onPreparedCallback();
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    void VEPlayer::forEachComponent(const std::function<void(const std::shared_ptr<IVEComponent> &)> &fn) {
        // 固定顺序遍历，缺失的组件(如纯音频文件没有视频链路)自动跳过
        const std::shared_ptr<IVEComponent> components[] = {
                mVideoRender, mAudioOutput, mVideoDecoder, mAudioDecoder, mDemux
        };
        for (const auto &component : components) {
            if (component) {
                fn(component);
            }
        }
    }

    VEResult VEPlayer::onStart(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mState == STATE_IDLE || mState == STATE_ERROR) {
            ALOGW("VEPlayer::%s ignored in state %d", __FUNCTION__, mState);
            return VE_INVALID_OPERATION;
        }
        if (mSeekStage != SEEK_STAGE_NONE) {
            // seek 流程会在结束时按 mStateBeforeSeek 恢复播放
            ALOGI("VEPlayer::%s during seek, resume after seek done", __FUNCTION__);
            mStateBeforeSeek = STATE_STARTED;
            return VE_OK;
        }

        mState = STATE_STARTED;
        if (mMediaClock) {
            mMediaClock->resume();
        }
        forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->start(); });
        startProgressTick();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::onStop(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        mSeekStage = SEEK_STAGE_NONE;
        mHasPendingSeek = false;
        mPendingAcks = 0;
        mAckContinuation = nullptr;
        mState = STATE_PREPARED;
        stopProgressTick();
        if (mMediaClock) {
            mMediaClock->resetClock();
        }
        forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->stop(); });
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::onPause(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mSeekStage != SEEK_STAGE_NONE) {
            mStateBeforeSeek = STATE_PAUSED;
            return VE_OK;
        }
        if (mState != STATE_STARTED) {
            ALOGW("VEPlayer::%s ignored in state %d", __FUNCTION__, mState);
            return VE_INVALID_OPERATION;
        }

        mState = STATE_PAUSED;
        stopProgressTick();
        if (mMediaClock) {
            mMediaClock->pause();
        }
        forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->pause(); });
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::onSeek(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        double timestampMs = 0;
        if (msg->findDouble("timestampMs", &timestampMs)) {
            startSeek(timestampMs);
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEPlayer::teardownComponents() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);

        // 中断正在进行的流程，避免拆解过程中还有回调想往下推进
        stopProgressTick();
        mSeekStage = SEEK_STAGE_NONE;
        mHasPendingSeek = false;
        mPendingAcks = 0;
        mAckContinuation = nullptr;
        ++mAckGeneration;

        // ① 先停数据流：demux 停止读取，解码器/渲染器停止消费
        forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->stop(); });

        // ② 投递释放消息。looper 停止时会把队列排空，这些消息保证会被执行到，
        //    编解码器上下文/EGL/SLES 都在各自的线程上销毁。
        forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->release(); });

        // ③ 逐个停止并 join 组件线程(会先排空队列，执行上面的释放消息)
        const std::shared_ptr<ALooper> loopers[] = {
                mVideoRenderLooper, mAudioOutputLooper,
                mVideoDecodeLooper, mAudioDecodeLooper, mDemuxLooper
        };
        for (const auto &looper : loopers) {
            if (looper) {
                looper->stop();
            }
        }

        // ④ 组件线程已退出，可以安全丢掉对象
        mVideoRender.reset();
        mAudioOutput.reset();
        mVideoDecoder.reset();
        mAudioDecoder.reset();
        mDemux.reset();

        mVideoRenderLooper.reset();
        mAudioOutputLooper.reset();
        mVideoDecodeLooper.reset();
        mAudioDecodeLooper.reset();
        mDemuxLooper.reset();

        mAVSync.reset();
        mMediaClock.reset();
        mMediaInfo.reset();

        mVideoEOS = false;
        mAudioEOS = false;
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onReset(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        // reset 后应能重新 setDataSource：必须真正拆掉这一套组件和线程，
        // 否则再次 setDataSource 会又建一套 looper，线程只增不减。
        teardownComponents();
        mPath.clear();
        mState = STATE_IDLE;
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::onRelease(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        teardownComponents();

        if (mWindow) {
            ANativeWindow_release(mWindow);
            mWindow = nullptr;
        }
        mState = STATE_IDLE;

        // release 是同步调用：调用方(Driver 析构)要等这里做完才能销毁 player looper
        std::shared_ptr<AReplyToken> replyID;
        if (msg->senderAwaitsResponse(replyID)) {
            std::shared_ptr<AMessage> response = std::make_shared<AMessage>();
            response->setInt32("ret", VE_OK);
            response->postReply(replyID);
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    void VEPlayer::setLooping(bool enable) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        mEnableLoop = enable;
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    long VEPlayer::getCurrentPosition() {
        // 直接读主时钟。可跨线程调用：VEMediaClock 内部自带锁。
        auto clock = mMediaClock;
        if (clock == nullptr) {
            return 0;
        }
        double positionUs = clock->getCurrentMediaTime();
        if (positionUs < 0) {
            positionUs = 0;
        }
        long positionMs = static_cast<long>(positionUs / 1000);

        // 时钟按实时外推，末尾可能略微超过总时长，这里夹住
        if (mMediaInfo != nullptr && mMediaInfo->duration > 0 &&
            positionMs > static_cast<long>(mMediaInfo->duration)) {
            positionMs = static_cast<long>(mMediaInfo->duration);
        }
        return positionMs;
    }

    long VEPlayer::getDuration() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mMediaInfo == nullptr) {
            ALOGE("VEPlayer mMediaInfo is null!!!");
            return VE_UNKNOWN_ERROR;
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return mMediaInfo->duration;
    }

    void VEPlayer::setVolume(int volume) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnInfoListener(funOnInfoCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onInfoCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnProgressListener(funOnProgressCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onProgressCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnCompletionListener(funOnCompletionCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onCompleteCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnErrorListener(funOnErrorCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onErrorCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnEOSListener(funOnEOSCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onEosCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnPreparedListener(funOnPreparedCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onPreparedCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    void VEPlayer::setOnSeekComplateListener(funOnSeekComplateCallback callback) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        onSeekComplateCallback = std::move(callback);
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::setPlaySpeed(float speed) {
        // 时钟侧支持变速，但音频还没做变速重采样(sonic 已链接未接入)。
        // 只改时钟会让音频仍按 1x 播放而时钟按 N 倍走，反而彻底破坏同步，
        // 因此这里明确返回不支持，避免上层以为设置成功。
        ALOGW("VEPlayer::%s speed=%f not supported yet (audio time-stretch missing)",
              __FUNCTION__, speed);
        return VE_INVALID_OPERATION;
    }

    void VEPlayer::onEOS() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mSeekStage != SEEK_STAGE_NONE) {
            // seek 过程中管线被 flush，此时的 EOS 不代表播放结束
            ALOGI("VEPlayer::%s ignored during seek", __FUNCTION__);
            return;
        }

        // 只统计实际存在的链路：纯音频/纯视频文件不该等一条永远不会到来的 EOS
        bool videoDone = (mVideoRender == nullptr) || mVideoEOS;
        bool audioDone = (mAudioOutput == nullptr) || mAudioEOS;

        if (videoDone && audioDone) {
            ALOGI("VEPlayer::%s play complate", __FUNCTION__);
            mVideoEOS = false;
            mAudioEOS = false;

            if (mEnableLoop) {
                // 循环播放：回到片头继续播，不上报播放结束。
                // 先置为 STARTED，seek 流程会据此在结束时恢复播放。
                ALOGI("VEPlayer::%s looping, seek to head", __FUNCTION__);
                mState = STATE_STARTED;
                startSeek(0);
                return;
            }

            mState = STATE_COMPLETED;
            stopProgressTick();
            // 收尾补一次满进度，避免进度条停在最后一次 tick 的位置
            if (mMediaInfo != nullptr && mMediaInfo->duration > 0) {
                notifyProgress(static_cast<int64_t>(mMediaInfo->duration) * 1000);
            }
            if (onCompleteCallback) {
                onCompleteCallback();
            }
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onSurfaceChanged(ANativeWindow *win, int viewWidth, int viewHeight) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        mWindow = win;
        mViewWidth = viewWidth;
        mViewHeight = viewHeight;

        // 只有在mVideoRender已经初始化后才调用setSurface
        if (mVideoRender != nullptr) {
            mVideoRender->setSurface(mWindow, mViewWidth, mViewHeight);
        }

        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onComponentEvent(const std::shared_ptr<AMessage> &msg) {
        int32_t type = EComponentType::E_COMPONENT_TYPE_UNKNOW;
        int32_t event = VE_NOTIFY_EVENT_UNKNOW;
        msg->findInt32("type", &type);
        msg->findInt32("event", &event);

        switch (event) {
            case VE_NOTIFY_EVENT_EOS: {
                if (type == EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER) {
                    mVideoEOS = true;
                } else if (type == EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER) {
                    mAudioEOS = true;
                }
                onEOS();
                break;
            }
            case VE_NOTIFY_EVENT_PROGRESS: {
                int64_t progress = 0;
                msg->findInt64("arg3", &progress);
                notifyProgress(progress);
                break;
            }
            case VE_NOTIFY_EVENT_ERROR: {
                int32_t code = 0;
                msg->findInt32("arg1", &code);
                mState = STATE_ERROR;
                notifyError(code, "component reported error");
                break;
            }
            case VE_NOTIFY_EVENT_FIRST_FRAME: {
                // 首帧既是 seek 完成的依据，也顺带更新一次进度
                int64_t pts = 0;
                msg->findInt64("arg3", &pts);
                notifyProgress(pts);
                onComponentAck(type, event);
                break;
            }
            default: {
                // SEEK_DONE / FLUSH_DONE / PAUSE_DONE / STOP_DONE 等命令回执
                onComponentAck(type, event);
                break;
            }
        }
        return VE_OK;
    }

    // ---------------------------------------------------------------------
    // 回执聚合
    // ---------------------------------------------------------------------

    uint32_t VEPlayer::activeComponentMask() const {
        uint32_t mask = 0;
        if (mVideoRender)  mask |= 1u << EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER;
        if (mAudioOutput)  mask |= 1u << EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER;
        if (mVideoDecoder) mask |= 1u << EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER;
        if (mAudioDecoder) mask |= 1u << EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER;
        if (mDemux)        mask |= 1u << EComponentType::E_COMPONENT_TYPE_DEMUX;
        return mask;
    }

    void VEPlayer::awaitAcks(uint32_t mask, int32_t event, std::function<void()> next) {
        mPendingAcks = mask;
        mExpectedAckEvent = event;
        mAckContinuation = std::move(next);
        ++mAckGeneration;

        ALOGI("VEPlayer::awaitAcks mask:0x%x event:%d gen:%d", mask, event, mAckGeneration);

        if (mPendingAcks == 0) {
            auto continuation = mAckContinuation;
            mAckContinuation = nullptr;
            mExpectedAckEvent = 0;
            if (continuation) {
                continuation();
            }
            return;
        }

        // 丢失回执时的兜底，避免流程永久停在某一阶段
        auto timeoutMsg = std::make_shared<AMessage>(kWhatAckTimeout, shared_from_this());
        timeoutMsg->setInt32("generation", mAckGeneration);
        timeoutMsg->post(kAckTimeoutUs);
    }

    void VEPlayer::onComponentAck(int32_t type, int32_t event) {
        if (mPendingAcks == 0 || event != mExpectedAckEvent) {
            ALOGD("VEPlayer::onComponentAck ignore type:%d event:%d (expect:%d pending:0x%x)",
                  type, event, mExpectedAckEvent, mPendingAcks);
            return;
        }
        if (type < 0) {
            return;
        }

        mPendingAcks &= ~(1u << type);
        ALOGI("VEPlayer::onComponentAck type:%d event:%d remaining:0x%x", type, event, mPendingAcks);

        if (mPendingAcks == 0) {
            auto continuation = mAckContinuation;
            mAckContinuation = nullptr;
            mExpectedAckEvent = 0;
            ++mAckGeneration;   // 让在途的超时消息失效
            if (continuation) {
                continuation();
            }
        }
    }

    // ---------------------------------------------------------------------
    // 进度上报
    // ---------------------------------------------------------------------

    void VEPlayer::startProgressTick() {
        ++mProgressGeneration;
        auto tick = std::make_shared<AMessage>(kWhatProgressTick, shared_from_this());
        tick->setInt32("generation", mProgressGeneration);
        tick->post();
    }

    void VEPlayer::stopProgressTick() {
        // 递增代次即可让在途的 tick 失效
        ++mProgressGeneration;
    }

    void VEPlayer::onProgressTick(const std::shared_ptr<AMessage> &msg) {
        int32_t generation = 0;
        msg->findInt32("generation", &generation);
        if (generation != mProgressGeneration || mState != STATE_STARTED) {
            return;
        }

        if (mMediaClock) {
            notifyProgress(static_cast<int64_t>(mMediaClock->getCurrentMediaTime()));
        }

        auto next = std::make_shared<AMessage>(kWhatProgressTick, shared_from_this());
        next->setInt32("generation", mProgressGeneration);
        next->post(kProgressIntervalUs);
    }

    void VEPlayer::onAckTimeout(const std::shared_ptr<AMessage> &msg) {
        int32_t generation = 0;
        msg->findInt32("generation", &generation);
        if (generation != mAckGeneration || mPendingAcks == 0) {
            return; // 回执已齐或已进入下一阶段
        }

        ALOGW("VEPlayer::onAckTimeout event:%d still pending:0x%x, continue anyway",
              mExpectedAckEvent, mPendingAcks);

        mPendingAcks = 0;
        auto continuation = mAckContinuation;
        mAckContinuation = nullptr;
        mExpectedAckEvent = 0;
        if (continuation) {
            continuation();
        }
    }

    // ---------------------------------------------------------------------
    // seek：分阶段推进，每阶段等齐回执后再进入下一阶段
    // ---------------------------------------------------------------------

    void VEPlayer::startSeek(double timestampMs) {
        if (mState == STATE_IDLE || mState == STATE_ERROR) {
            ALOGW("VEPlayer::startSeek ignored in state %d", mState);
            return;
        }

        if (mSeekStage != SEEK_STAGE_NONE) {
            // 正在 seek：只保留最后一次请求，完成后补做(拖动进度条的关键)
            ALOGI("VEPlayer::startSeek coalesce pending seek to %f", timestampMs);
            mHasPendingSeek = true;
            mPendingSeekMs = timestampMs;
            return;
        }

        ALOGI("VEPlayer::startSeek to %f ms", timestampMs);
        mSeekTargetMs = timestampMs;
        mStateBeforeSeek = (mState == STATE_SEEKING) ? mStateBeforeSeek : mState;
        mState = STATE_SEEKING;
        mVideoEOS = false;
        mAudioEOS = false;

        seekStagePause();
    }

    void VEPlayer::seekStagePause() {
        // ① 先让所有组件停止消费数据，避免 flush 与解码并发
        ALOGI("VEPlayer::seek stage 1/3 - pausing components");
        mSeekStage = SEEK_STAGE_PAUSING;
        if (mAVSync) {
            mMediaClock->pause();
        }

        uint32_t mask = activeComponentMask();
        forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->pause(); });
        awaitAcks(mask, VE_NOTIFY_EVENT_PAUSE_DONE, [this] { seekStageSeek(); });
    }

    void VEPlayer::seekStageSeek() {
        // ② demux 定位到目标关键帧，解码器 flush 并记下精准 seek 目标
        ALOGI("VEPlayer::seek stage 2/3 - seeking demux & flushing decoders");
        mSeekStage = SEEK_STAGE_SEEKING;

        uint32_t mask = activeComponentMask();
        double target = mSeekTargetMs;
        forEachComponent([target](const std::shared_ptr<IVEComponent> &c) { c->seekTo(target); });
        awaitAcks(mask, VE_NOTIFY_EVENT_SEEK_DONE, [this] { seekStagePrime(); });
    }

    void VEPlayer::seekStagePrime() {
        // ③ 把时钟重新定位到目标位置后重启管线，等第一帧真正上屏
        ALOGI("VEPlayer::seek stage 3/3 - priming first frame");
        mSeekStage = SEEK_STAGE_PRIMING;

        if (mAVSync) {
            mMediaClock->resetTo(mSeekTargetMs * 1000.0);
            mAVSync->reset(mSeekTargetMs * 1000.0);
        }

        if (mVideoRender) {
            // 有视频时以首帧上屏作为 seek 完成的判据；
            // 暂停态下也要出这一帧，否则 seek 后画面不会更新。
            if (mDemux)        mDemux->start();
            if (mVideoDecoder) mVideoDecoder->start();
            if (mAudioDecoder) mAudioDecoder->start();
            mVideoRender->start();
            if (mStateBeforeSeek == STATE_STARTED && mAudioOutput) {
                mAudioOutput->start();
            }
            uint32_t mask = 1u << EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER;
            awaitAcks(mask, VE_NOTIFY_EVENT_FIRST_FRAME, [this] { seekFinish(); });
        } else {
            // 纯音频：没有画面可等，恢复播放即视为完成
            if (mStateBeforeSeek == STATE_STARTED) {
                forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->start(); });
            }
            seekFinish();
        }
    }

    void VEPlayer::seekFinish() {
        ALOGI("VEPlayer::seekFinish, restore state %d", mStateBeforeSeek);
        mSeekStage = SEEK_STAGE_NONE;

        if (mStateBeforeSeek == STATE_STARTED) {
            mState = STATE_STARTED;
            forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->start(); });
            if (mMediaClock) {
                mMediaClock->resume();
            }
            // 进度 tick 在 seek 期间(状态为 SEEKING)已经自行中断，这里必须重新拉起
            startProgressTick();
        } else {
            // 暂停态 seek：预览帧已经上屏，重新回到暂停
            mState = STATE_PAUSED;
            forEachComponent([](const std::shared_ptr<IVEComponent> &c) { c->pause(); });
            if (mMediaClock) {
                // 冻结时钟，否则暂停期间它会一直外推，恢复播放时视频会被判定为落后
                mMediaClock->pause();
            }
        }

        // seek 完成后立刻上报一次位置，不必等下一个 tick 周期
        if (mMediaClock) {
            notifyProgress(static_cast<int64_t>(mMediaClock->getCurrentMediaTime()));
        }

        if (onSeekComplateCallback) {
            onSeekComplateCallback();
        }

        if (mHasPendingSeek) {
            // seek 期间被合并掉的请求，现在补做
            mHasPendingSeek = false;
            startSeek(mPendingSeekMs);
        }
    }
}