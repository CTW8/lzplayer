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

        /// 拆解阶段的回执超时。release 是同步调用(通常来自主线程的 onDestroy)，
        /// 两个阶段各等 2s 会逼近 ANR 阈值，所以这里收紧。
        constexpr int64_t kTeardownAckTimeoutUs = 800000;

        /// 拆解进行中收到 setDataSource/reset/release 时的重投间隔：
        /// 把请求排到当前拆解完成之后，而不是并发再起一轮拆解
        constexpr int64_t kDeferWhileReleasingUs = 20000;
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
                onFlowTimeout(msg);
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
                // window 为空是合法输入(surface 销毁)，同样要往下传，
                // 否则渲染器会一直画向已失效的窗口
                onSurfaceChanged(window, width, height);
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
        if (mState == STATE_RELEASING) {
            // 上一轮拆解还在握手中。此刻再起一轮拆解会丢掉它的 continuation
            // 并对半释放的组件重发命令，正确性全押在组件幂等上。
            // 改为把整条消息排到拆解完成之后重新处理。
            msg->post(kDeferWhileReleasingUs);
            return VE_OK;
        }
        std::string path;
        if (!msg->findString("path", path) || path.empty()) {
            ALOGE("VEPlayer::%s - Invalid path", __FUNCTION__);
            return VE_UNKNOWN_ERROR; // Define this error code
        }
        // 允许不经 reset 直接换片源：先拆掉上一套组件(异步握手)，
        // 拆干净了再建新链路，否则会再建一套 looper 导致线程只增不减
        if (mDemux != nullptr) {
            ALOGI("VEPlayer::%s tear down previous pipeline first", __FUNCTION__);
            teardownComponents([this, path] { setupDataSource(path); });
            return VE_OK;
        }

        return setupDataSource(path);
    }

    VEResult VEPlayer::setupDataSource(const std::string &path) {
        ALOGI("VEPlayer::%s path:%s", __FUNCTION__, path.c_str());
        mPath = path;
        mState = STATE_IDLE;

        mRenderNotifyMsg = std::make_shared<AMessage>(kWhatComponentEvent, shared_from_this());
        // 管线代次盖在 notify 模板上(组件 dup 时自动携带)：
        // 上一代管线被强推拆掉后，其迟到事件无法污染新管线
        mRenderNotifyMsg->setInt32("plGen", ++mPipelineGen);

        mDemuxLooper = std::make_shared<ALooper>();
        mDemuxLooper->setName("demux_thread");
        mDemuxLooper->start(false);

        mDemux = std::make_shared<VEDemux>(mRenderNotifyMsg);
        mDemuxLooper->registerHandler(mDemux);
        mSourceState = ROLE_ACTIVE;
        return VE_OK;
    }

    VEResult VEPlayer::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mState == STATE_RELEASING) {
            // 拆解期间不能对半释放的 demux 发 prepare，排到拆解完成后
            msg->post(kDeferWhileReleasingUs);
            return VE_OK;
        }
        if (mDemux == nullptr) {
            // reset 之后没有重新 setDataSource 就 prepare
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "no data source!!");
            return VE_UNKNOWN_ERROR;
        }
        if (mDemux->prepare(mPath) != VE_OK) {
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "demux open failed!!");
            return VE_UNKNOWN_ERROR;
        }

        {
            // 与 getCurrentPosition/getDuration 的跨线程读互斥
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaInfo = mDemux->getFileInfo();
        }
        if (mMediaInfo == nullptr ||
            (mMediaInfo->audio_stream_index == -1 && mMediaInfo->video_stream_index == -1)) {
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "no playable stream found!!");
            return VE_UNKNOWN_ERROR;
        }
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaClock = std::make_shared<VEMediaClock>();
        }
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
            mAudioOutput->prepare(std::static_pointer_cast<IMediaDecoder>(mAudioDecoder),
                                  audioOut);
            mAudioDecState = ROLE_ACTIVE;
            mAudioRenderState = ROLE_ACTIVE;
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

            mVideoRender->prepare(std::static_pointer_cast<IMediaDecoder>(mVideoDecoder),
                                  mWindow, mViewWidth, mViewHeight, mMediaInfo->fps);
            mVideoDecState = ROLE_ACTIVE;
            mVideoDisplayState = ROLE_ACTIVE;
        }

        mState = STATE_PREPARED;
        if (onPreparedCallback) {
            onPreparedCallback();
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return 0;
    }

    VEResult VEPlayer::onStart(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mState == STATE_IDLE || mState == STATE_ERROR || mState == STATE_RELEASING) {
            ALOGW("VEPlayer::%s ignored in state %d", __FUNCTION__, mState);
            return VE_INVALID_OPERATION;
        }
        if (mSeekStage != SEEK_STAGE_NONE) {
            // seek 流程会在结束时按 mStateBeforeSeek 恢复播放
            ALOGI("VEPlayer::%s during seek, resume after seek done", __FUNCTION__);
            mStateBeforeSeek = STATE_STARTED;
            return VE_OK;
        }
        if (mState == STATE_COMPLETED) {
            // 播完后 start 应回片头重播(对齐 MediaPlayer 语义)。
            // 直接 start 的话 demux/解码器都停在 EOS，会立刻再次"播完"。
            ALOGI("VEPlayer::%s restart from head after completion", __FUNCTION__);
            mState = STATE_STARTED;
            startSeek(0);
            return VE_OK;
        }

        mState = STATE_STARTED;
        if (mMediaClock) {
            if (mAudioOutput == nullptr && !mMediaClock->isAnchored()) {
                // 纯视频文件没有音频帧驱动时钟：首次起播手动起锚。
                // 否则时钟恒为 0，每帧按自己的绝对 pts 各自等待，
                // 开场呈现累计数秒的慢动作
                mMediaClock->resetTo(0.0);
            }
            mMediaClock->resume();
        }
        if (mVideoRender)  mVideoRender->start();
        if (mAudioOutput)  mAudioOutput->start();
        if (mVideoDecoder) mVideoDecoder->start();
        if (mAudioDecoder) mAudioDecoder->start();
        if (mDemux)        mDemux->start();
        startProgressTick();
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEPlayer::onStop(std::shared_ptr<AMessage> msg) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        if (mState == STATE_RELEASING) {
            ALOGW("VEPlayer::%s ignored while releasing", __FUNCTION__);
            return VE_INVALID_OPERATION;
        }
        mSeekStage = SEEK_STAGE_NONE;
        mHasPendingSeek = false;
        ++mFlowSeq;                 // 作废在途的 seek 阶段超时
        setAllRoles(ROLE_ACTIVE);   // seek 中途被 stop：角色收拢回稳态
        mState = STATE_PREPARED;
        stopProgressTick();
        if (mMediaClock) {
            mMediaClock->resetClock();
        }
        if (mVideoRender)  mVideoRender->stop();
        if (mAudioOutput)  mAudioOutput->stop();
        if (mVideoDecoder) mVideoDecoder->stop();
        if (mAudioDecoder) mAudioDecoder->stop();
        if (mDemux)        mDemux->stop();
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
        if (mVideoRender)  mVideoRender->pause();
        if (mAudioOutput)  mAudioOutput->pause();
        if (mVideoDecoder) mVideoDecoder->pause();
        if (mAudioDecoder) mAudioDecoder->pause();
        if (mDemux)        mDemux->pause();
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

    void VEPlayer::teardownComponents(std::function<void()> onDone) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);

        // 中断正在进行的流程，避免拆解过程中还有回调想往下推进
        stopProgressTick();
        mSeekStage = SEEK_STAGE_NONE;
        mHasPendingSeek = false;
        mState = STATE_RELEASING;
        mTeardownDone = std::move(onDone);
        ++mFlowSeq;

        if (!anyRoleExists()) {
            // 没有任何组件(重复 reset/release)：直接收尾
            finishTeardownAndContinue();
            return;
        }

        // ① 停数据流：demux 停止读取，解码器/渲染器停止消费。
        //    每个存在的角色都要回 STOP_DONE 才能确认没有组件还在动数据。
        ALOGI("VEPlayer::teardown stage 1/2 - stopping data flow");
        if (mVideoRender)  { mVideoRender->stop();  mVideoDisplayState = ROLE_STOPPING; }
        if (mAudioOutput)  { mAudioOutput->stop();  mAudioRenderState  = ROLE_STOPPING; }
        if (mVideoDecoder) { mVideoDecoder->stop(); mVideoDecState     = ROLE_STOPPING; }
        if (mAudioDecoder) { mAudioDecoder->stop(); mAudioDecState     = ROLE_STOPPING; }
        if (mDemux)        { mDemux->stop();        mSourceState       = ROLE_STOPPING; }
        postFlowTimeout(kTeardownAckTimeoutUs);
    }

    void VEPlayer::enterTeardownReleaseStage() {
        // ② 释放资源。编解码器上下文/EGL/SLES 都必须在各自的线程上销毁，
        //    所以只能投递消息过去，等 RELEASE_DONE 回执确认做完。
        ALOGI("VEPlayer::teardown stage 2/2 - releasing component resources");
        ++mFlowSeq;
        if (mVideoRender)  { mVideoRender->release();  mVideoDisplayState = ROLE_RELEASING; }
        if (mAudioOutput)  { mAudioOutput->release();  mAudioRenderState  = ROLE_RELEASING; }
        if (mVideoDecoder) { mVideoDecoder->release(); mVideoDecState     = ROLE_RELEASING; }
        if (mAudioDecoder) { mAudioDecoder->release(); mAudioDecState     = ROLE_RELEASING; }
        if (mDemux)        { mDemux->release();        mSourceState       = ROLE_RELEASING; }
        postFlowTimeout(kTeardownAckTimeoutUs);
    }

    void VEPlayer::finishTeardownAndContinue() {
        // ③ 资源已释放干净，此时停 looper 丢消息也不会漏掉任何清理
        ++mFlowSeq;
        finishTeardown();
        auto done = std::move(mTeardownDone);
        mTeardownDone = nullptr;
        if (done) {
            done();
        }
    }

    void VEPlayer::finishTeardown() {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);

        // 停止并 join 组件线程。资源已在上一步释放完，
        // 队列里即便还有残留消息也只是过期的解码/渲染消息，丢掉无妨。
        const std::shared_ptr<ALooper> loopers[] = {
                mVideoRenderLooper, mAudioOutputLooper,
                mVideoDecodeLooper, mAudioDecodeLooper, mDemuxLooper
        };
        const std::shared_ptr<AHandler> handlers[] = {
                mVideoRender, mAudioOutput, mVideoDecoder, mAudioDecoder, mDemux
        };
        for (size_t i = 0; i < sizeof(loopers) / sizeof(loopers[0]); ++i) {
            if (loopers[i] && handlers[i]) {
                loopers[i]->unregisterHandler(handlers[i]->id());
            }
            if (loopers[i]) {
                loopers[i]->stop();
            }
        }

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
        {
            // 与 getCurrentPosition/getDuration 的跨线程读互斥
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaClock.reset();
            mMediaInfo.reset();
        }

        mVideoEOS = false;
        mAudioEOS = false;
        setAllRoles(ROLE_NONE);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onReset(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        if (mState == STATE_RELEASING) {
            // 排到当前拆解完成之后再处理，避免并发两轮拆解
            msg->post(kDeferWhileReleasingUs);
            return VE_OK;
        }
        // reset 后应能重新 setDataSource：必须真正拆掉这一套组件和线程，
        // 否则再次 setDataSource 会又建一套 looper，线程只增不减。
        teardownComponents([this] {
            mPath.clear();
            mState = STATE_IDLE;
            ALOGI("VEPlayer::onReset done");
        });
        return VE_OK;
    }

    VEResult VEPlayer::onRelease(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        if (mState == STATE_RELEASING) {
            // reset/换源触发的拆解还在进行：整条消息(连同 replyToken)排到
            // 它完成之后。届时组件已拆净，本次 release 走空掩码快速路径回复。
            msg->post(kDeferWhileReleasingUs);
            return VE_OK;
        }

        // release 是同步调用(Driver 析构在等)，但拆解要跨多轮消息握手，
        // 所以先扣下 replyToken，等整条链走完再回复。
        std::shared_ptr<AReplyToken> replyID;
        bool wantsReply = msg->senderAwaitsResponse(replyID);

        teardownComponents([this, replyID, wantsReply] {
            if (mWindow) {
                ANativeWindow_release(mWindow);
                mWindow = nullptr;
            }
            mState = STATE_IDLE;
            ALOGI("VEPlayer::onRelease done");

            if (wantsReply) {
                std::shared_ptr<AMessage> response = std::make_shared<AMessage>();
                response->setInt32("ret", VE_OK);
                response->postReply(replyID);
            }
        });
        return VE_OK;
    }

    void VEPlayer::setLooping(bool enable) {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        mEnableLoop = enable;
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
    }

    long VEPlayer::getCurrentPosition() {
        // 可跨线程调用：先在锁下取 shared_ptr 副本(与 prepare/teardown 的
        // 赋值/reset 互斥)，之后读时钟由 VEMediaClock 自己的锁保护
        std::shared_ptr<VEMediaClock> clock;
        std::shared_ptr<VEMediaInfo> info;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            clock = mMediaClock;
            info = mMediaInfo;
        }
        if (clock == nullptr) {
            return 0;
        }
        double positionUs = clock->getCurrentMediaTime();
        if (positionUs < 0) {
            positionUs = 0;
        }
        long positionMs = static_cast<long>(positionUs / 1000);

        // 时钟按实时外推，末尾可能略微超过总时长，这里夹住
        if (info != nullptr && info->duration > 0 &&
            positionMs > static_cast<long>(info->duration)) {
            positionMs = static_cast<long>(info->duration);
        }
        return positionMs;
    }

    long VEPlayer::getDuration() {
        ALOGV("VEPlayer::%s enter", __FUNCTION__);
        std::shared_ptr<VEMediaInfo> info;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            info = mMediaInfo;
        }
        if (info == nullptr) {
            ALOGE("VEPlayer mMediaInfo is null!!!");
            // 未 prepare 时长未知：返回 0 而不是 INT32_MIN，上层拿去算
            // 进度条不至于得到荒谬值
            return 0;
        }
        ALOGV("VEPlayer::%s exit", __FUNCTION__);
        return info->duration;
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

            // 到达流尾时先发 EOS 通知(循环播放也会发)，
            // 是否"播放完成"由下面的分支决定
            if (onEosCallback) {
                onEosCallback();
            }

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
        if (mWindow != nullptr && mWindow != win) {
            // JNI 层每次 setSurface 都 acquire 一个新引用，旧引用必须在
            // 覆盖前释放，否则转屏/前后台一次就漏一个 window
            ANativeWindow_release(mWindow);
        }
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

        // 管线代次校验：上一代管线的迟到事件一律丢弃
        int32_t plGen = 0;
        msg->findInt32("plGen", &plGen);
        if (plGen != mPipelineGen) {
            ALOGW("VEPlayer::%s stale pipeline event type:%d event:%d gen:%d cur:%d",
                  __FUNCTION__, type, event, plGen, mPipelineGen);
            return VE_OK;
        }

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
                if (mSeekStage == SEEK_STAGE_PRIMING &&
                    type == EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER) {
                    seekFinish();
                } else {
                    ALOGD("VEPlayer::%s first frame outside priming, ignore", __FUNCTION__);
                }
                break;
            }
            case VE_NOTIFY_EVENT_PAUSE_DONE: {
                // 只在 seek 阶段①被接受；fire-and-forget pause 的回执在此被丢弃
                if (mSeekStage == SEEK_STAGE_PAUSING &&
                    acceptAck(type, ROLE_PAUSING, ROLE_PAUSED) &&
                    rolesAllIn(ROLE_PAUSED)) {
                    seekStageSeek();
                }
                break;
            }
            case VE_NOTIFY_EVENT_SEEK_DONE: {
                if (mSeekStage == SEEK_STAGE_SEEKING &&
                    acceptAck(type, ROLE_SEEKING, ROLE_SEEK_DONE) &&
                    rolesAllIn(ROLE_SEEK_DONE)) {
                    seekStagePrime();
                }
                break;
            }
            case VE_NOTIFY_EVENT_STOP_DONE: {
                // 只在 teardown 阶段①被接受；onStop 的 fire-and-forget 回执被丢弃
                if (mState == STATE_RELEASING &&
                    acceptAck(type, ROLE_STOPPING, ROLE_STOPPED) &&
                    rolesAllIn(ROLE_STOPPED)) {
                    enterTeardownReleaseStage();
                }
                break;
            }
            case VE_NOTIFY_EVENT_RELEASE_DONE: {
                if (mState == STATE_RELEASING &&
                    acceptAck(type, ROLE_RELEASING, ROLE_RELEASED) &&
                    rolesAllIn(ROLE_RELEASED)) {
                    finishTeardownAndContinue();
                }
                break;
            }
            default: {
                ALOGD("VEPlayer::%s unhandled event type:%d event:%d", __FUNCTION__, type, event);
                break;
            }
        }
        return VE_OK;
    }

    // ---------------------------------------------------------------------
    // 角色状态机
    // ---------------------------------------------------------------------

    VEPlayer::RoleState *VEPlayer::roleStateFor(int32_t type) {
        switch (type) {
            case EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER:  return &mVideoDisplayState;
            case EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER:  return &mAudioRenderState;
            case EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER: return &mVideoDecState;
            case EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER: return &mAudioDecState;
            case EComponentType::E_COMPONENT_TYPE_DEMUX:         return &mSourceState;
            default:                                             return nullptr;
        }
    }

    bool VEPlayer::acceptAck(int32_t type, RoleState expectIng, RoleState done) {
        RoleState *state = roleStateFor(type);
        if (state == nullptr) {
            return false;
        }
        if (*state != expectIng) {
            // 过期/重复回执：角色不在等待态，丢弃。这是防污染的核心守卫。
            ALOGW("VEPlayer::acceptAck drop stale ack type:%d state:%d expect:%d",
                  type, *state, expectIng);
            return false;
        }
        *state = done;
        ALOGI("VEPlayer::acceptAck type:%d -> %d", type, done);
        return true;
    }

    bool VEPlayer::rolesAllIn(RoleState done) const {
        const RoleState roles[] = { mSourceState, mAudioDecState, mVideoDecState,
                                    mAudioRenderState, mVideoDisplayState };
        for (RoleState s : roles) {
            if (s != ROLE_NONE && s != done) {
                return false;
            }
        }
        return true;
    }

    void VEPlayer::setAllRoles(RoleState s) {
        if (mSourceState != ROLE_NONE)       mSourceState = s;
        if (mAudioDecState != ROLE_NONE)     mAudioDecState = s;
        if (mVideoDecState != ROLE_NONE)     mVideoDecState = s;
        if (mAudioRenderState != ROLE_NONE)  mAudioRenderState = s;
        if (mVideoDisplayState != ROLE_NONE) mVideoDisplayState = s;
    }

    bool VEPlayer::anyRoleExists() const {
        return mSourceState != ROLE_NONE || mAudioDecState != ROLE_NONE ||
               mVideoDecState != ROLE_NONE || mAudioRenderState != ROLE_NONE ||
               mVideoDisplayState != ROLE_NONE;
    }

    void VEPlayer::postFlowTimeout(int64_t delayUs) {
        auto timeoutMsg = std::make_shared<AMessage>(kWhatAckTimeout, shared_from_this());
        timeoutMsg->setInt32("seq", mFlowSeq);
        timeoutMsg->post(delayUs > 0 ? delayUs : kAckTimeoutUs);
    }

    void VEPlayer::onFlowTimeout(const std::shared_ptr<AMessage> &msg) {
        int32_t seq = 0;
        msg->findInt32("seq", &seq);
        if (seq != mFlowSeq) {
            return; // 阶段已推进，超时兜底作废
        }

        if (mState == STATE_RELEASING) {
            // teardown 超时：release 必须有界，强推收尾。
            // 阶段①卡住 → 跳过等待直接进入释放阶段；阶段②卡住 → 直接收尾。
            // 残余风险(旧组件迟到事件)由 pipelineGen 兜底。
            if (rolesAllIn(ROLE_RELEASED) || mSourceState == ROLE_RELEASING ||
                mAudioDecState == ROLE_RELEASING || mVideoDecState == ROLE_RELEASING ||
                mAudioRenderState == ROLE_RELEASING || mVideoDisplayState == ROLE_RELEASING) {
                ALOGW("VEPlayer::onFlowTimeout teardown release stage timed out, force finish");
                finishTeardownAndContinue();
            } else {
                ALOGW("VEPlayer::onFlowTimeout teardown stop stage timed out, force release");
                enterTeardownReleaseStage();
            }
            return;
        }

        if (mSeekStage != SEEK_STAGE_NONE) {
            // seek 阶段超时：不再"强推装作完成"(那是过期回执的制造者)，
            // 组件卡死就是故障，如实报错并收敛。
            ALOGE("VEPlayer::onFlowTimeout seek stage %d timed out", mSeekStage);
            abortSeekOnTimeout();
        }
    }

    void VEPlayer::abortSeekOnTimeout() {
        ++mFlowSeq;
        mSeekStage = SEEK_STAGE_NONE;
        mHasPendingSeek = false;
        setAllRoles(ROLE_ACTIVE);
        stopProgressTick();
        if (mMediaClock) {
            mMediaClock->pause();
        }
        mState = STATE_ERROR;
        notifyError(VE_TIMED_OUT, "seek timed out, component not responding");
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

    // ---------------------------------------------------------------------
    // seek：分阶段推进，每阶段等齐回执后再进入下一阶段
    // ---------------------------------------------------------------------

    void VEPlayer::startSeek(double timestampMs) {
        if (mState == STATE_IDLE || mState == STATE_ERROR || mState == STATE_RELEASING) {
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
        ++mFlowSeq;
        if (mMediaClock) {
            mMediaClock->pause();
        }

        if (mVideoRender)  { mVideoRender->pause();  mVideoDisplayState = ROLE_PAUSING; }
        if (mAudioOutput)  { mAudioOutput->pause();  mAudioRenderState  = ROLE_PAUSING; }
        if (mVideoDecoder) { mVideoDecoder->pause(); mVideoDecState     = ROLE_PAUSING; }
        if (mAudioDecoder) { mAudioDecoder->pause(); mAudioDecState     = ROLE_PAUSING; }
        if (mDemux)        { mDemux->pause();        mSourceState       = ROLE_PAUSING; }
        postFlowTimeout(kAckTimeoutUs);
    }

    void VEPlayer::seekStageSeek() {
        // ② demux 定位到目标关键帧，解码器 flush 并记下精准 seek 目标
        ALOGI("VEPlayer::seek stage 2/3 - seeking demux & flushing decoders");
        mSeekStage = SEEK_STAGE_SEEKING;
        ++mFlowSeq;

        const double target = mSeekTargetMs;
        if (mVideoRender)  { mVideoRender->seekTo(target);  mVideoDisplayState = ROLE_SEEKING; }
        if (mAudioOutput)  { mAudioOutput->seekTo(target);  mAudioRenderState  = ROLE_SEEKING; }
        if (mVideoDecoder) { mVideoDecoder->seekTo(target); mVideoDecState     = ROLE_SEEKING; }
        if (mAudioDecoder) { mAudioDecoder->seekTo(target); mAudioDecState     = ROLE_SEEKING; }
        if (mDemux)        { mDemux->seekTo(target);        mSourceState       = ROLE_SEEKING; }
        postFlowTimeout(kAckTimeoutUs);
    }

    void VEPlayer::seekStagePrime() {
        // ③ 把时钟重新定位到目标位置后重启管线，等第一帧真正上屏
        ALOGI("VEPlayer::seek stage 3/3 - priming first frame");
        mSeekStage = SEEK_STAGE_PRIMING;
        ++mFlowSeq;
        // 本轮 seek 的命令-回执循环到此为止，角色收拢回稳态；
        // 阶段③只等视频显示的 FIRST_FRAME 事件(由 mSeekStage 守卫)
        setAllRoles(ROLE_ACTIVE);

        if (mMediaClock) {
            mMediaClock->resetTo(mSeekTargetMs * 1000.0);
        }
        if (mAVSync) {
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
            postFlowTimeout(kAckTimeoutUs);
        } else {
            // 纯音频：没有画面可等，恢复播放即视为完成
            if (mStateBeforeSeek == STATE_STARTED) {
                if (mAudioOutput)  mAudioOutput->start();
                if (mAudioDecoder) mAudioDecoder->start();
                if (mDemux)        mDemux->start();
            }
            seekFinish();
        }
    }

    void VEPlayer::seekFinish() {
        ALOGI("VEPlayer::seekFinish, restore state %d", mStateBeforeSeek);
        mSeekStage = SEEK_STAGE_NONE;
        ++mFlowSeq;   // 作废阶段③的超时兜底
        setAllRoles(ROLE_ACTIVE);

        if (mStateBeforeSeek == STATE_STARTED) {
            mState = STATE_STARTED;
            if (mVideoRender)  mVideoRender->start();
            if (mAudioOutput)  mAudioOutput->start();
            if (mVideoDecoder) mVideoDecoder->start();
            if (mAudioDecoder) mAudioDecoder->start();
            if (mDemux)        mDemux->start();
            if (mMediaClock) {
                mMediaClock->resume();
            }
            // 进度 tick 在 seek 期间(状态为 SEEKING)已经自行中断，这里必须重新拉起
            startProgressTick();
        } else {
            // 暂停态 seek：预览帧已经上屏，重新回到暂停
            mState = STATE_PAUSED;
            if (mVideoRender)  mVideoRender->pause();
            if (mAudioOutput)  mAudioOutput->pause();
            if (mVideoDecoder) mVideoDecoder->pause();
            if (mAudioDecoder) mAudioDecoder->pause();
            if (mDemux)        mDemux->pause();
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