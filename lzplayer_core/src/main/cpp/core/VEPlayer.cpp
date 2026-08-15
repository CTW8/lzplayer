#include "VEPlayer.h"
#include "platform/android/decoders/VECodecWarmup.h"
#include "VEDemux.h"
#include "VESourceRegistry.h"
#include "decoders/VEMediaCodecVideoDecoder.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"

#include <algorithm>
#include <utility>
#include <vector>
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

        /// 变速范围。超出这个区间 sonic 的音质会明显劣化，
        /// 且 0.5x 下输出样本翻倍、2x 下丢半，缓冲预留按此上界算。
        constexpr float kMinPlaybackSpeed = 0.5f;
        constexpr float kMaxPlaybackSpeed = 2.0f;

    }

    VEPlayer::VEPlayer() {
    }

    VEPlayer::~VEPlayer() {
    }

    VEResult VEPlayer::setDataSource(std::string path) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetDataSource,
                                                                   shared_from_this());
        msg->setString("path", path);
        msg->post();
        return 0;
    }

    VEResult VEPlayer::setDisplayOut(ANativeWindow *win, int viewWidth, int viewHeight) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetVideoSurface,shared_from_this());
        msg->setPointer("window",win);
        msg->setInt32("viewWidth",viewWidth);
        msg->setInt32("viewHeight",viewHeight);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::prepare() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,
                                                                   shared_from_this());

//    std::shared_ptr<AMessage> respon;
//    msg->postAndAwaitResponse(&respon);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::prepareAsync() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare,
                                                                   shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEPlayer::start() {
        std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
        return 0;
    }

    VEResult VEPlayer::stop() {
        std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
        return 0;
    }

    VEResult VEPlayer::pause() {
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        return 0;
    }

    VEResult VEPlayer::release() {
        // 同步等待：返回后各组件线程都已退出、资源已释放，调用方才能安全销毁播放器
        auto msg = std::make_shared<AMessage>(kWhatRelease, shared_from_this());
        std::shared_ptr<AMessage> response;
        if (msg->postAndAwaitResponse(&response) != OK) {
            ALOGW("VEPlayer::%s post release failed, looper may be gone", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    VEResult VEPlayer::seek(double timestampMs) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestampMs", timestampMs);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::reset() {
        std::make_shared<AMessage>(kWhatReset, shared_from_this())->post();
        return 0;
    }

    void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
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
            case kWhatSetSpeed: {
                onSetSpeed(msg);
                break;
            }
            case kWhatSelectTrack: {
                onSelectTrack(msg);
                break;
            }
            case kWhatAddSubtitle: {
                onAddSubtitle(msg);
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
    }

    VEResult VEPlayer::onSetDataSource(std::shared_ptr<AMessage> msg) {
        std::string path;
        if (!msg->findString("path", path) || path.empty()) {
            ALOGE("VEPlayer::%s - Invalid path", __FUNCTION__);
            return VE_UNKNOWN_ERROR; // Define this error code
        }
        if (isFlowBusy()) {
            // 典型时序：reset 的拆解还在握手中就来了 setDataSource。
            // 入队排到拆解完成之后执行，不并发两轮流程。
            PendingAction action;
            action.type = PendingAction::ACTION_SET_DATA_SOURCE;
            action.path = path;
            mPendingActions.push_back(action);
            return VE_OK;
        }
        if (mSource != nullptr) {
            // 对齐 AOSP MediaPlayer 语义：换片源必须先 reset。
            // 原来的"拆旧建新"隐式换源路径删除(Driver 侧本来就挡死)。
            ALOGE("VEPlayer::%s must reset before setting a new source", __FUNCTION__);
            return VE_INVALID_OPERATION;
        }

        return setupDataSource(path);
    }

    VEResult VEPlayer::setupDataSource(const std::string &path) {
        ALOGI("VEPlayer::%s path:%s", __FUNCTION__, path.c_str());
        mPath = path;
        mState = STATE_IDLE;
        // 换片源：清掉上一轮 fallback 留下的记忆，重新给硬解一次机会
        mDecoderPolicy.forceSoftware = mUserForceSoftware.load();
        mVideoHardware = false;

        mRenderNotifyMsg = std::make_shared<AMessage>(kWhatComponentEvent, shared_from_this());
        // 管线代次盖在 notify 模板上(组件 dup 时自动携带)：
        // 上一代管线被强推拆掉后，其迟到事件无法污染新管线
        mRenderNotifyMsg->setInt32("plGen", ++mPipelineGen);

        mSourceLooper = std::make_shared<ALooper>();
        mSourceLooper->setName("demux_thread");
        mSourceLooper->start(false);

        mSource = createSource(path);
        if (mSource == nullptr) {
            // 不支持的 scheme：拆掉刚起的 looper，报错给上层
            mSourceLooper->stop();
            mSourceLooper.reset();
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "unsupported source scheme");
            return VE_UNKNOWN_ERROR;
        }
        mSourceLooper->registerHandler(mSource);
        setRole(ROLE_IDX_SOURCE, mSource, mSourceLooper,
                EComponentType::E_COMPONENT_TYPE_DEMUX);
        return VE_OK;
    }

    std::shared_ptr<VESource> VEPlayer::createSource(const std::string &path) {
        ALOGI("VEPlayer::%s path:%s", __FUNCTION__, path.c_str());
        // 按 scheme 查注册表：file → VEDemux，http/https → 网络源，
        // 未来任何协议只要注册一条工厂就能接进来，这里不必改动
        return VESourceRegistry::instance().create(path, mRenderNotifyMsg);
    }

    VEResult VEPlayer::onPrepare(std::shared_ptr<AMessage> msg) {
        // T0 打在最外层：即使因拆解未完而排队，等待时间也属于用户感知的
        // 启播耗时，必须记进来(单独作为 queueWait 段列出，不与打开容器混算)
        if (mStartupTrace == nullptr) {
            mStartupTrace = std::make_shared<VEStartupTrace>();
        }
        if (mPerfStats == nullptr) {
            mPerfStats = std::make_shared<VEPerfStats>();
        }
        mStartupTrace->reset();
        mPerfStats->reset();
        mTimeline.reset();
        mStartupTrace->mark(VEStartupTrace::T0_REQUEST);
        if (isFlowBusy()) {
            // 拆解期间不能对半释放的 demux 发 prepare，排队执行
            PendingAction action;
            action.type = PendingAction::ACTION_PREPARE;
            mPendingActions.push_back(action);
            return VE_OK;
        }
        return doPrepare();
    }

    VEResult VEPlayer::doPrepare() {
        if (mSource == nullptr) {
            // reset 之后没有重新 setDataSource 就 prepare
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "no data source!!");
            return VE_UNKNOWN_ERROR;
        }
        // 异步 prepare：player looper 不再被 avformat_open_input 挂住，
        // 期间到来的命令由 Flow 队列排队；PREPARE_DONE 回执驱动建链后半段
        mState = STATE_PREPARING;
        if (mStartupTrace != nullptr) {
            // 排队等待到此结束，T1~T3 由源在自己的 looper 上打点
            mStartupTrace->mark(VEStartupTrace::T0_DISPATCH);
            mSource->setStartupTrace(mStartupTrace);
            if (auto demux = std::dynamic_pointer_cast<VEDemux>(mSource)) {
                demux->setPerfStats(mPerfStats);
            }
        }
        mSource->prepareAsync(mPath);
        return VE_OK;
    }

    VEResult VEPlayer::continuePrepare() {
        {
            // 与 getCurrentPosition/getDuration 的跨线程读互斥
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaInfo = mSource->getFileInfo();
        }
        if (mMediaInfo == nullptr ||
            (!mMediaInfo->hasAudio() && !mMediaInfo->hasVideo())) {
            mState = STATE_ERROR;
            notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "no playable stream found!!");
            return VE_UNKNOWN_ERROR;
        }
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaClock = std::make_shared<VEMediaClock>();
        }
        mAVSync = std::make_shared<VEAVsync>(mMediaClock);
        if (mMediaInfo->fps() > 0) {
            mAVSync->setFrameRate(mMediaInfo->fps());
        }

        if (mMediaInfo->hasAudio()) {
            mAudioDecodeLooper = std::make_shared<ALooper>();
            mAudioDecodeLooper->setName("adec_thread");
            mAudioDecodeLooper->start(false);

            // 输出参数只在这里算一次，解码器和渲染器共用，避免两处各写死一份
            const VEAudioOutputConfig audioOut =
                    chooseAudioOutputConfig(mMediaInfo->sampleRate(), mMediaInfo->channels());
            ALOGI("VEPlayer::%s audio src %dHz %dch -> out %dHz %dch", __FUNCTION__,
                  mMediaInfo->sampleRate(), mMediaInfo->channels(),
                  audioOut.sampleRate, audioOut.channels);

            // 推模型下先建渲染端(sink)，解码器建链时把 sink 交给它
            mAudioOutputLooper = std::make_shared<ALooper>();
            mAudioOutputLooper->setName("audio_render");
            mAudioOutputLooper->start(false);

            mAudioOutput = std::make_shared<VEAudioRender>(mRenderNotifyMsg, mAVSync);
            mAudioOutputLooper->registerHandler(mAudioOutput);
            // 测试开关必须在 prepare 之前设：后端在 prepare 里就选定了
            mAudioOutput->setForceSles(mForceSlesAudio.load());
            mAudioOutput->setStartupTrace(mStartupTrace);
            mAudioOutput->prepare(audioOut);

            mAudioDecoder = std::make_shared<VEAudioDecoder>(mRenderNotifyMsg);
            mAudioDecodeLooper->registerHandler(mAudioDecoder);
            mAudioDecoder->setPerfStats(mPerfStats);
            mAudioDecoder->prepare(mSource, audioOut,
                                   std::static_pointer_cast<IFrameSink>(mAudioOutput));
            setRole(ROLE_IDX_AUDIO_DECODER, mAudioDecoder, mAudioDecodeLooper,
                    EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER);
            setRole(ROLE_IDX_AUDIO_RENDER, mAudioOutput, mAudioOutputLooper,
                    EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER);
        }

        if (mMediaInfo->hasVideo()) {
            setupVideoChain();
        }

        mState = STATE_PREPARED;
        if (mStartupTrace != nullptr) {
            // 只有真的有视频轨才谈解码路径。纯音频文件 mVideoHardware 恒为
            // false，直接上报会变成 "software"——对照报告里看见这个值的人
            // 会以为它跑了软解视频，从而拿它和真正的软解数据比。
            if (mMediaInfo != nullptr && mMediaInfo->hasVideo()) {
                mStartupTrace->setDecodePath(mVideoHardware);
            }
            mStartupTrace->mark(VEStartupTrace::T4_CHAIN_READY);
        }
        if (onPreparedCallback) {
            onPreparedCallback();
        }
        return 0;
    }

    VEResult VEPlayer::setupVideoChain() {
        const VETrackInfo *track = mMediaInfo->videoTrack();
        if (track == nullptr) {
            return VE_INVALID_OPERATION;
        }
        // 用户显式要求的强制软解优先于一切；运行期 fallback 也会置这个位，
        // 两者取或——一旦回退过就不再自动试硬解，避免来回抖
        if (mUserForceSoftware.load()) {
            mDecoderPolicy.forceSoftware = true;
        }

        mVideoDecodeLooper = std::make_shared<ALooper>();
        mVideoDecodeLooper->setName("vdec_thread");
        mVideoDecodeLooper->start(false);

        mVideoDecoder = VEVideoDecoderFactory::create(
                *track, mWindow, mDecoderPolicy, mRenderNotifyMsg, mAVSync,
                &mVideoHardware);
        mVideoDecodeLooper->registerHandler(
                std::dynamic_pointer_cast<AHandler>(mVideoDecoder));

        if (mVideoHardware) {
            // 硬解直出 Surface：解码 + 同步 + 上屏都在这一个组件里，
            // 因此它同时占据解码与显示两个角色槽位，各回一份回执。
            // VEPlayer 的分阶段握手因此完全不必区分软硬解。
            VEBundle params;
            params.set("surface", mWindow);
            mVideoDecoder->setStartupTrace(mStartupTrace);
            mVideoDecoder->setPerfStats(mPerfStats);
            mVideoDecoder->prepare(mSource, nullptr, params);
            setRole(ROLE_IDX_VIDEO_DECODER, mVideoDecoder, mVideoDecodeLooper,
                    EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER);
            setRole(ROLE_IDX_VIDEO_DISPLAY, mVideoDecoder, mVideoDecodeLooper,
                    EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER);
            return VE_OK;
        }

        // 软解：显示端是独立组件，推模型下先建 sink 再把它交给解码器
        mVideoRenderLooper = std::make_shared<ALooper>();
        mVideoRenderLooper->setName("video_render");
        mVideoRenderLooper->start(false);
        mVideoRender = std::make_shared<VEVideoDisplay>(mRenderNotifyMsg, mAVSync);
        mVideoRenderLooper->registerHandler(mVideoRender);
        // 与 setForceSles 同样的时机约束：必须在 prepare 之前
        mVideoRender->setPreferVulkan(mPreferVulkanRender.load());
        mVideoRender->prepare(mWindow, mViewWidth, mViewHeight, mMediaInfo->fps(),
                              mMediaInfo->rotationDegrees(),
                              track->width, track->height);

        mVideoRender->setStartupTrace(mStartupTrace);
        mVideoRender->setPerfStats(mPerfStats);
        mVideoDecoder->setStartupTrace(mStartupTrace);
        mVideoDecoder->setPerfStats(mPerfStats);
        mVideoDecoder->prepare(mSource,
                               std::static_pointer_cast<IFrameSink>(mVideoRender),
                               VEBundle());
        setRole(ROLE_IDX_VIDEO_DECODER, mVideoDecoder, mVideoDecodeLooper,
                EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER);
        setRole(ROLE_IDX_VIDEO_DISPLAY, mVideoRender, mVideoRenderLooper,
                EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER);
        return VE_OK;
    }

    void VEPlayer::rebuildVideoAsSoftware() {
        ALOGW("VEPlayer::%s hardware decoder failed, rebuilding with software",
              __FUNCTION__);
        const double resumeMs = mMediaClock
                ? mMediaClock->getCurrentMediaTime() / 1000.0 : 0.0;
        const bool wasPlaying = (mState == STATE_STARTED);

        // 拆掉视频两个角色(硬解时是同一个组件占两格)
        auto comp = mRoles[ROLE_IDX_VIDEO_DECODER].comp;
        auto looper = mRoles[ROLE_IDX_VIDEO_DECODER].looper;
        if (comp) {
            comp->stop();
            comp->release();
        }
        if (looper) {
            auto handler = std::dynamic_pointer_cast<AHandler>(comp);
            if (handler) looper->unregisterHandler(handler->id());
            looper->stop();
        }
        auto displayLooper = mRoles[ROLE_IDX_VIDEO_DISPLAY].looper;
        if (displayLooper && displayLooper != looper) {
            auto handler = std::dynamic_pointer_cast<AHandler>(
                    mRoles[ROLE_IDX_VIDEO_DISPLAY].comp);
            if (handler) displayLooper->unregisterHandler(handler->id());
            displayLooper->stop();
        }
        setRole(ROLE_IDX_VIDEO_DECODER, nullptr, nullptr,
                EComponentType::E_COMPONENT_TYPE_UNKNOW);
        setRole(ROLE_IDX_VIDEO_DISPLAY, nullptr, nullptr,
                EComponentType::E_COMPONENT_TYPE_UNKNOW);
        mVideoDecoder.reset();
        mVideoRender.reset();
        mVideoDecodeLooper.reset();
        mVideoRenderLooper.reset();

        // 换代次：旧硬解组件的迟到事件不能污染新链路
        mRenderNotifyMsg->setInt32("plGen", ++mPipelineGen);

        mDecoderPolicy.forceSoftware = true;
        mVideoHardware = false;
        if (setupVideoChain() != VE_OK) {
            mState = STATE_ERROR;
            notifyError(VE_UNKNOWN_ERROR, "video chain rebuild failed");
            return;
        }
        // 通知上层已降级(信息类，不是错误)。走 ON_INFO 通道、把降级原因放 arg1：
        // VE_INFO_DECODER_FALLBACK 本身不是 Java 侧认识的事件号，
        // 直接拿它当事件号发会在 JNI 分发处被当成未知消息丢掉。
        notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_INFO, VE_INFO_DECODER_FALLBACK,
                   "decoder fallback to software", nullptr);

        // 回到中断前的位置续播
        mState = wasPlaying ? STATE_STARTED : STATE_PAUSED;
        startSeek(resumeMs);
    }

    // ---------------------------------------------------------------------
    // 多轨与字幕
    // ---------------------------------------------------------------------

    std::string VEPlayer::getTrackInfoJson() {
        std::shared_ptr<VEMediaInfo> info;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            info = mMediaInfo;
        }
        std::string json = "[";
        if (info) {
            bool first = true;
            for (const auto &t : info->tracks) {
                if (!first) json += ",";
                first = false;
                const char *typeStr = (t.type == ETrackType::AUDIO) ? "audio"
                                    : (t.type == ETrackType::VIDEO) ? "video" : "subtitle";
                const bool active =
                        (t.type == ETrackType::AUDIO && info->activeAudio >= 0 &&
                         info->tracks[info->activeAudio].index == t.index) ||
                        (t.type == ETrackType::VIDEO && info->activeVideo >= 0 &&
                         info->tracks[info->activeVideo].index == t.index) ||
                        (t.type == ETrackType::SUBTITLE && info->activeSubtitle >= 0 &&
                         info->tracks[info->activeSubtitle].index == t.index);
                const AVCodec *codec = avcodec_find_decoder(t.codecId);
                json += "{\"index\":" + std::to_string(t.index) +
                        ",\"type\":\"" + typeStr + "\"" +
                        ",\"lang\":\"" + t.lang + "\"" +
                        ",\"title\":\"" + t.title + "\"" +
                        ",\"codec\":" + std::to_string(static_cast<int>(t.codecId)) +
                        ",\"codecName\":\"" + std::string(codec && codec->name ? codec->name : "-") + "\"" +
                        ",\"sampleRate\":" + std::to_string(t.sampleRate) +
                        ",\"channels\":" + std::to_string(t.channels) +
                        ",\"width\":" + std::to_string(t.width) +
                        ",\"height\":" + std::to_string(t.height) +
                        ",\"rotation\":" + std::to_string(t.rotationDegrees) +
                        ",\"active\":" + (active ? "true" : "false") + "}";
            }
        }
        json += "]";
        return json;
    }

    VEResult VEPlayer::selectTrack(int trackIndex) {
        auto msg = std::make_shared<AMessage>(kWhatSelectTrack, shared_from_this());
        msg->setInt32("trackIndex", trackIndex);
        msg->setInt32("deselect", 0);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::deselectTrack(int trackIndex) {
        auto msg = std::make_shared<AMessage>(kWhatSelectTrack, shared_from_this());
        msg->setInt32("trackIndex", trackIndex);
        msg->setInt32("deselect", 1);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::addExternalSubtitle(const std::string &path) {
        auto msg = std::make_shared<AMessage>(kWhatAddSubtitle, shared_from_this());
        msg->setString("path", path);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::onSelectTrack(const std::shared_ptr<AMessage> &msg) {
        int32_t trackIndex = -1;
        int32_t deselect = 0;
        msg->findInt32("trackIndex", &trackIndex);
        msg->findInt32("deselect", &deselect);

        if (isFlowBusy()) {
            // 切轨是长流程，与 seek/reset 串行化(覆盖"切轨中 seek"
            // 与"seek 中切轨"两种并发)
            PendingAction action;
            action.type = PendingAction::ACTION_SELECT_TRACK;
            action.trackIndex = trackIndex;
            action.deselect = (deselect != 0);
            mPendingActions.push_back(action);
            return VE_OK;
        }
        return doSelectTrack(trackIndex, deselect != 0);
    }

    VEResult VEPlayer::doSelectTrack(int trackIndex, bool deselect) {
        if (mMediaInfo == nullptr || mSource == nullptr) {
            return VE_INVALID_OPERATION;
        }

        // 外挂字幕虚拟轨：不经 demux，直接把内存 cue 交给字幕组件
        auto ext = mExternalCues.find(trackIndex);
        if (!deselect && ext != mExternalCues.end()) {
            if (setupSubtitleChain() != VE_OK) {
                return VE_UNKNOWN_ERROR;
            }
            mSubtitle->setExternalCues(ext->second);
            mSubtitle->setSpeed(mPlaybackSpeed);
            if (mState == STATE_STARTED) {
                mSubtitle->start();
            }
            notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED, trackIndex,
                       "external subtitle selected", nullptr);
            return VE_OK;
        }

        const int slot = mMediaInfo->slotOfTrackIndex(trackIndex);
        if (slot < 0) {
            ALOGE("VEPlayer::%s unknown track %d", __FUNCTION__, trackIndex);
            return VE_PLAYER_ERROR_UNSUPPORTED_TRACK;
        }
        const ETrackType type = mMediaInfo->tracks[slot].type;

        if (deselect) {
            if (type != ETrackType::SUBTITLE) {
                // 关掉音/视频轨没有合理语义(会直接静音/黑屏)
                return VE_PLAYER_ERROR_UNSUPPORTED_TRACK;
            }
            mSource->selectTrack(-1);
            if (mSubtitle) {
                mSubtitle->stop();
            }
            mMediaInfo->activeSubtitle = -1;
            notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED, -1,
                       "subtitle deselected", nullptr);
            return VE_OK;
        }

        switch (type) {
            case ETrackType::SUBTITLE: {
                // 轻量路径：换流 + 清 cue，不必重建解码器也不必全链 seek
                if (setupSubtitleChain() != VE_OK) {
                    return VE_UNKNOWN_ERROR;
                }
                mSource->selectTrack(trackIndex);
                mMediaInfo->activeSubtitle = slot;
                mSubtitle->flush();
                mSubtitle->setSpeed(mPlaybackSpeed);
                if (mState == STATE_STARTED) {
                    mSubtitle->start();
                }
                notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED, trackIndex,
                           "subtitle track changed", nullptr);
                return VE_OK;
            }
            case ETrackType::AUDIO:
                return switchAudioTrack(trackIndex);
            case ETrackType::VIDEO:
                // 视频轨切换牵动整条渲染链与硬解绑定，本期不支持
                ALOGE("VEPlayer::%s video track switch not supported", __FUNCTION__);
                return VE_PLAYER_ERROR_UNSUPPORTED_TRACK;
        }
        return VE_OK;
    }

    VEResult VEPlayer::switchAudioTrack(int trackIndex) {
        const int slot = mMediaInfo->slotOfTrackIndex(trackIndex);
        const VETrackInfo &newTrack = mMediaInfo->tracks[slot];
        const VETrackInfo *oldTrack = mMediaInfo->audioTrack();
        if (oldTrack && oldTrack->index == trackIndex) {
            return VE_OK;   // 已经是它了
        }

        const double resumeMs = mMediaClock ? mMediaClock->getCurrentMediaTime() / 1000.0 : 0.0;
        const bool sameFormat =
                oldTrack != nullptr &&
                oldTrack->codecId == newTrack.codecId &&
                oldTrack->sampleRate == newTrack.sampleRate &&
                oldTrack->channels == newTrack.channels;

        ALOGI("VEPlayer::%s -> track %d (%s), resume at %.0f ms", __FUNCTION__,
              trackIndex, sameFormat ? "same format" : "rebuild", resumeMs);

        // demux 换流：读位置还停在原处，靠随后的全链 seek 拉回播放位置。
        // 单 AVFormatContext 下这一步无法只影响音频——所以视频链也要
        // 跟着重解当前 GOP，表现为画面短暂定格(不清屏，因此不黑屏)。
        mSource->selectTrack(trackIndex);
        mMediaInfo->activeAudio = slot;

        if (!sameFormat && mAudioDecoder && mAudioOutput) {
            // 参数变了：解码器与设备都要按新轨重建
            const VEAudioOutputConfig audioOut =
                    chooseAudioOutputConfig(newTrack.sampleRate, newTrack.channels);
            mAudioDecoder->stop();
            mAudioDecoder->release();
            mAudioOutput->stop();
            mAudioOutput->release();

            if (mAudioDecodeLooper) {
                mAudioDecodeLooper->unregisterHandler(mAudioDecoder->id());
                mAudioDecodeLooper->stop();
            }
            if (mAudioOutputLooper) {
                mAudioOutputLooper->unregisterHandler(mAudioOutput->id());
                mAudioOutputLooper->stop();
            }

            mAudioOutputLooper = std::make_shared<ALooper>();
            mAudioOutputLooper->setName("audio_render");
            mAudioOutputLooper->start(false);
            mAudioOutput = std::make_shared<VEAudioRender>(mRenderNotifyMsg, mAVSync);
            mAudioOutputLooper->registerHandler(mAudioOutput);
            mAudioOutput->prepare(audioOut);
            if (mPlaybackSpeed != 1.0f) {
                mAudioOutput->setSpeed(mPlaybackSpeed, resumeMs * 1000.0);
            }

            mAudioDecodeLooper = std::make_shared<ALooper>();
            mAudioDecodeLooper->setName("adec_thread");
            mAudioDecodeLooper->start(false);
            mAudioDecoder = std::make_shared<VEAudioDecoder>(mRenderNotifyMsg);
            mAudioDecodeLooper->registerHandler(
                    std::dynamic_pointer_cast<AHandler>(mAudioDecoder));
            mAudioDecoder->prepare(mSource, audioOut,
                                   std::static_pointer_cast<IFrameSink>(mAudioOutput));

            setRole(ROLE_IDX_AUDIO_DECODER, mAudioDecoder, mAudioDecodeLooper,
                    EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER);
            setRole(ROLE_IDX_AUDIO_RENDER, mAudioOutput, mAudioOutputLooper,
                    EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER);
        }

        // 全链精准 seek 回当前位置：新轨从这里开始出声，视频重解当前 GOP
        startSeek(resumeMs);
        notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED, trackIndex,
                   "audio track changed", nullptr);
        return VE_OK;
    }

    VEResult VEPlayer::setupSubtitleChain() {
        if (mSubtitle != nullptr) {
            return VE_OK;
        }
        mSubtitleLooper = std::make_shared<ALooper>();
        mSubtitleLooper->setName("subtitle_thread");
        mSubtitleLooper->start(false);
        mSubtitle = std::make_shared<VESubtitleTrack>(mRenderNotifyMsg, mMediaClock);
        mSubtitleLooper->registerHandler(mSubtitle);
        // 注册进 Role 表：从此它像其它组件一样参与 seek/teardown 握手
        setRole(ROLE_IDX_SUBTITLE, mSubtitle, mSubtitleLooper,
                EComponentType::E_COMPONENT_TYPE_SUBTITLE);
        return VE_OK;
    }

    VEResult VEPlayer::onAddSubtitle(const std::shared_ptr<AMessage> &msg) {
        std::string path;
        if (!msg->findString("path", path) || path.empty()) {
            return VE_INVALID_PARAMS;
        }
        // 外挂字幕文件很小(MB 级)，一次性读完解成 cue 列表，
        // 之后完全脱离 demux —— 时间轴直接对主时钟
        AVFormatContext *ctx = nullptr;
        if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) != 0) {
            ALOGE("VEPlayer::%s open subtitle file failed: %s", __FUNCTION__, path.c_str());
            return VE_UNKNOWN_ERROR;
        }
        if (avformat_find_stream_info(ctx, nullptr) < 0) {
            avformat_close_input(&ctx);
            return VE_UNKNOWN_ERROR;
        }
        int streamIndex = -1;
        for (unsigned i = 0; i < ctx->nb_streams; ++i) {
            if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                streamIndex = static_cast<int>(i);
                break;
            }
        }
        if (streamIndex < 0) {
            avformat_close_input(&ctx);
            return VE_INVALID_PARAMS;
        }

        AVStream *stream = ctx->streams[streamIndex];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *dec = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (dec == nullptr ||
            avcodec_parameters_to_context(dec, stream->codecpar) < 0 ||
            avcodec_open2(dec, codec, nullptr) != 0) {
            if (dec) avcodec_free_context(&dec);
            avformat_close_input(&ctx);
            return VE_UNKNOWN_ERROR;
        }

        std::vector<VESubtitleTrack::Cue> cues;
        AVPacket *pkt = av_packet_alloc();
        while (pkt && av_read_frame(ctx, pkt) >= 0) {
            if (pkt->stream_index == streamIndex) {
                AVSubtitle sub;
                memset(&sub, 0, sizeof(sub));
                int got = 0;
                if (avcodec_decode_subtitle2(dec, &sub, &got, pkt) >= 0 && got) {
                    const int64_t basePts = (pkt->pts != AV_NOPTS_VALUE)
                            ? av_rescale_q(pkt->pts, stream->time_base, AV_TIME_BASE_Q) : 0;
                    int64_t startUs = basePts + sub.start_display_time * 1000;
                    int64_t endUs = basePts + sub.end_display_time * 1000;
                    if (endUs <= startUs) {
                        endUs = startUs + (pkt->duration > 0
                                ? av_rescale_q(pkt->duration, stream->time_base, AV_TIME_BASE_Q)
                                : 3000000);
                    }
                    for (unsigned i = 0; i < sub.num_rects; ++i) {
                        const AVSubtitleRect *r = sub.rects[i];
                        if (r == nullptr) continue;
                        std::string text;
                        if (r->type == SUBTITLE_ASS && r->ass) {
                            text = VESubtitleTrack::stripAss(r->ass);
                        } else if (r->type == SUBTITLE_TEXT && r->text) {
                            text = r->text;
                        }
                        if (!text.empty()) {
                            cues.push_back({startUs, endUs, text});
                        }
                    }
                    avsubtitle_free(&sub);
                }
            }
            av_packet_unref(pkt);
        }
        if (pkt) av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&ctx);

        if (cues.empty()) {
            ALOGW("VEPlayer::%s no cues parsed from %s", __FUNCTION__, path.c_str());
            return VE_UNKNOWN_ERROR;
        }

        // 登记成虚拟轨(轨道号 >= kExternalTrackIndexBase，与容器内流区分)
        const int trackIndex = mNextExternalTrackIndex++;
        mExternalCues[trackIndex] = std::move(cues);
        if (mMediaInfo) {
            VETrackInfo t;
            t.index = trackIndex;
            t.streamIndex = -1;
            t.type = ETrackType::SUBTITLE;
            t.title = path.substr(path.find_last_of('/') + 1);
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaInfo->tracks.push_back(t);
        }
        ALOGI("VEPlayer::%s external subtitle loaded as track %d (%zu cues)",
              __FUNCTION__, trackIndex, mExternalCues[trackIndex].size());
        return VE_OK;
    }

    void VEPlayer::setForceSoftwareDecoder(bool force) {
        mUserForceSoftware = force;
        ALOGI("VEPlayer::%s force software decoder = %d (takes effect on next prepare)",
              __FUNCTION__, force);
    }

    void VEPlayer::setForceSlesAudio(bool force) {
        mForceSlesAudio = force;
        ALOGI("VEPlayer::%s force SLES audio = %d (takes effect on next prepare)",
              __FUNCTION__, force);
    }

    void VEPlayer::setPreferVulkanRender(bool prefer) {
        mPreferVulkanRender = prefer;
        ALOGI("VEPlayer::%s prefer Vulkan render = %d (takes effect on next prepare, "
              "software decode only)", __FUNCTION__, prefer);
    }

    std::string VEPlayer::getStartupTraceJson() {
        // 可跨线程调用(JNI 线程)：trace 自身加锁，这里只需防 shared_ptr 撕裂
        std::shared_ptr<VEStartupTrace> trace;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            trace = mStartupTrace;
        }
        if (trace == nullptr) {
            return "{\"valid\":false}";
        }
        return trace->toJson();
    }

    std::string VEPlayer::getSeekTraceJson() {
        std::shared_ptr<VESeekTrace> trace;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            trace = mSeekTrace;
        }
        if (trace == nullptr) {
            return "{\"count\":0,\"items\":[]}";
        }
        return trace->toJson();
    }

    std::string VEPlayer::getStatsJson() {
        // 可跨线程调用：先在锁下取 shared_ptr 副本，之后各对象自己保证线程安全
        std::shared_ptr<VEMediaClock> clock;
        std::shared_ptr<VEMediaInfo> info;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            clock = mMediaClock;
            info = mMediaInfo;
        }

        const char *stateName = "IDLE";
        switch (mState) {
            case STATE_PREPARING: stateName = "PREPARING"; break;
            case STATE_PREPARED:  stateName = "PREPARED";  break;
            case STATE_STARTED:   stateName = "STARTED";   break;
            case STATE_PAUSED:    stateName = "PAUSED";    break;
            case STATE_SEEKING:   stateName = "SEEKING";   break;
            case STATE_COMPLETED: stateName = "COMPLETED"; break;
            case STATE_RELEASING: stateName = "RELEASING"; break;
            case STATE_ERROR:     stateName = "ERROR";     break;
            default: break;
        }

        int64_t rendered = 0, dropped = 0;
        if (mVideoHardware) {
            auto hw = std::dynamic_pointer_cast<VEMediaCodecVideoDecoder>(mVideoDecoder);
            if (hw) { rendered = hw->renderedFrames(); dropped = hw->droppedFrames(); }
        } else if (mVideoRender) {
            rendered = mVideoRender->renderedFrames();
            dropped = mVideoRender->droppedFrames();
        }

        int audioQueue = 0, videoQueue = 0;
        int64_t bufferedUs = 0;
        const char *sourceKind = "none";
        if (mSource) {
            auto demux = std::dynamic_pointer_cast<VEDemux>(mSource);
            if (demux) {
                audioQueue = demux->getQueueDepth(ETrackType::AUDIO);
                videoQueue = demux->getQueueDepth(ETrackType::VIDEO);
                bufferedUs = demux->getBufferedDurationUs();
            }
            sourceKind = (VESourceRegistry::schemeOf(mPath) == "file") ? "local" : "network";
        }

        const int64_t avOffsetUs = mAVSync ? mAVSync->getLastDiffUs() : 0;
        const char *audioBackend = mAudioOutput ? mAudioOutput->backendName() : "none";
        const long long audioUnderruns =
                mAudioOutput ? (long long) mAudioOutput->underrunCount() : -1;
        const char *videoCodecName = "-";
        if (info) {
            const VETrackInfo *v = info->videoTrack();
            if (v) {
                const AVCodec *c = avcodec_find_decoder(v->codecId);
                if (c && c->name) videoCodecName = c->name;
            }
        }

        // 定长 buf + 单次 snprintf 撑不住了：加上稳态分位数与队列峰值后
        // 输出会逼近并越过 768，而 snprintf 是**静默截断**——结果是一个缺
        // 右花括号的 JSON，Java 侧直接抛解析异常，现象却是"面板忽然全空"，
        // 极难定位。改成 std::string 拼接，长度不再是隐患。
        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "{\"state\":\"%s\",\"decoder\":\"%s\",\"codec\":\"%s\","
                 "\"audioBackend\":\"%s\",\"avOffsetMs\":%lld,"
                 "\"renderedFrames\":%lld,\"droppedFrames\":%lld,"
                 "\"audioQueue\":%d,\"videoQueue\":%d,\"bufferedMs\":%lld,"
                 "\"source\":\"%s\",\"speed\":%.2f,\"buffering\":%s,"
                 "\"positionMs\":%lld,\"durationMs\":%lld,"
                 "\"audioUnderruns\":%lld,",
                 stateName,
                 mVideoHardware ? "hardware" : (mMediaInfo && mMediaInfo->hasVideo() ? "software" : "-"),
                 videoCodecName, audioBackend,
                 static_cast<long long>(avOffsetUs / 1000),
                 static_cast<long long>(rendered), static_cast<long long>(dropped),
                 audioQueue, videoQueue,
                 static_cast<long long>(bufferedUs / 1000),
                 sourceKind, mPlaybackSpeed, mBuffering ? "true" : "false",
                 static_cast<long long>(clock ? clock->getCurrentMediaTime() / 1000 : 0),
                 static_cast<long long>(info ? info->duration : 0),
                 audioUnderruns);
        std::string out(buf);
        std::shared_ptr<VEPerfStats> perf;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            perf = mPerfStats;
        }
        if (perf) {
            out += perf->toJsonFragment();
        } else {
            out += "\"videoDecodeMs\":{\"p50\":-1,\"p95\":-1,\"max\":-1,\"n\":0}";
        }
        out += "}";
        return out;
    }

    VEResult VEPlayer::onStart(std::shared_ptr<AMessage> msg) {
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
        // T5 打在这里而不是函数入口：入口之后还有"seek 中"与"播完重播"
        // 两个提前 return 的分支，那两种情况并不是一次真正的起播
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T5_START);
        }
        if (mMediaClock) {
            if (mAudioOutput == nullptr && !mMediaClock->isAnchored()) {
                // 纯视频文件没有音频帧驱动时钟：首次起播手动起锚。
                // 否则时钟恒为 0，每帧按自己的绝对 pts 各自等待，
                // 开场呈现累计数秒的慢动作
                mMediaClock->resetTo(0.0);
            }
            mMediaClock->resume();
        }
        forEachRole([](Role &r) { r.comp->start(); });
        startProgressTick();
        return VE_OK;
    }

    VEResult VEPlayer::onStop(std::shared_ptr<AMessage> msg) {
        if (mState == STATE_RELEASING) {
            ALOGW("VEPlayer::%s ignored while releasing", __FUNCTION__);
            return VE_INVALID_OPERATION;
        }
        mSeekStage = SEEK_STAGE_NONE;
        mAbortSeek = false;
        mBuffering = false;
        dropQueuedSeeks();          // stop 后排队的 seek 已无意义
        ++mFlowSeq;                 // 作废在途的 seek 阶段超时
        setAllRoles(ROLE_ACTIVE);   // seek 中途被 stop：角色收拢回稳态
        mState = STATE_PREPARED;
        stopProgressTick();
        if (mMediaClock) {
            mMediaClock->resetClock();
        }
        forEachRole([](Role &r) { r.comp->stop(); });
        // stop 可能中断了在途 seek，排队的 reset/release 需要在这里接力
        processPendingActions();
        return VE_OK;
    }

    VEResult VEPlayer::onPause(std::shared_ptr<AMessage> msg) {
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
        forEachRole([](Role &r) { r.comp->pause(); });
        return VE_OK;
    }

    VEResult VEPlayer::onSeek(std::shared_ptr<AMessage> msg) {
        double timestampMs = 0;
        if (msg->findDouble("timestampMs", &timestampMs)) {
            startSeek(timestampMs);
        }
        return VE_OK;
    }

    // ---------------------------------------------------------------------
    // 操作串行化：长流程(seek/reset/release/prepare)不重叠
    // ---------------------------------------------------------------------

    bool VEPlayer::isFlowBusy() const {
        return mSeekStage != SEEK_STAGE_NONE || mState == STATE_RELEASING ||
               mState == STATE_PREPARING;
    }

    void VEPlayer::processPendingActions() {
        while (!mPendingActions.empty() && !isFlowBusy()) {
            PendingAction action = mPendingActions.front();
            mPendingActions.pop_front();
            switch (action.type) {
                case PendingAction::ACTION_SEEK:
                    startSeek(action.seekMs);
                    break;
                case PendingAction::ACTION_SET_DATA_SOURCE:
                    if (mSource != nullptr) {
                        ALOGE("VEPlayer::processPendingActions must reset before new source");
                    } else {
                        setupDataSource(action.path);
                    }
                    break;
                case PendingAction::ACTION_PREPARE:
                    doPrepare();
                    break;
                case PendingAction::ACTION_RESET:
                    executeReset();
                    break;
                case PendingAction::ACTION_RELEASE:
                    executeRelease(action.reply, action.wantsReply);
                    break;
                case PendingAction::ACTION_SET_SPEED:
                    doSetSpeed(action.speed);
                    break;
                case PendingAction::ACTION_REBUILD_VIDEO:
                    rebuildVideoAsSoftware();
                    break;
                case PendingAction::ACTION_SELECT_TRACK:
                    doSelectTrack(action.trackIndex, action.deselect);
                    break;
            }
        }
    }

    void VEPlayer::dropQueuedSeeks() {
        for (auto it = mPendingActions.begin(); it != mPendingActions.end();) {
            if (it->type == PendingAction::ACTION_SEEK) {
                it = mPendingActions.erase(it);
            } else {
                ++it;
            }
        }
    }

    void VEPlayer::abortSeekForAction() {
        // reset/release 在排队等待：本轮 seek 到此为止，不再进入下一阶段。
        // 播放状态不必恢复——紧接着的 reset/release 会拆掉整条管线。
        ALOGI("VEPlayer::%s seek aborted for pending reset/release", __FUNCTION__);
        ++mFlowSeq;
        mSeekStage = SEEK_STAGE_NONE;
        setAllRoles(ROLE_ACTIVE);
        mAbortSeek = false;
        processPendingActions();
    }

    void VEPlayer::teardownComponents(std::function<void()> onDone) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);

        // 中断正在进行的流程，避免拆解过程中还有回调想往下推进
        stopProgressTick();
        mSeekStage = SEEK_STAGE_NONE;
        mAbortSeek = false;
        mBuffering = false;
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
        forEachRole([](Role &r) { r.comp->stop(); r.state = ROLE_STOPPING; });
        postFlowTimeout(kTeardownAckTimeoutUs);
    }

    void VEPlayer::enterTeardownReleaseStage() {
        // ② 释放资源。编解码器上下文/EGL/SLES 都必须在各自的线程上销毁，
        //    所以只能投递消息过去，等 RELEASE_DONE 回执确认做完。
        ALOGI("VEPlayer::teardown stage 2/2 - releasing component resources");
        ++mFlowSeq;
        forEachRole([](Role &r) { r.comp->release(); r.state = ROLE_RELEASING; });
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
        // 同一个组件可能占多个角色位(硬解解码器)，looper 去重后再停。
        std::vector<std::shared_ptr<ALooper>> stopped;
        for (auto &role : mRoles) {
            if (!role.looper) {
                continue;
            }
            if (std::find(stopped.begin(), stopped.end(), role.looper) != stopped.end()) {
                continue;
            }
            stopped.push_back(role.looper);
            auto handler = std::dynamic_pointer_cast<AHandler>(role.comp);
            if (handler) {
                role.looper->unregisterHandler(handler->id());
            }
            role.looper->stop();
        }
        for (auto &role : mRoles) {
            role.comp.reset();
            role.looper.reset();
            role.state = ROLE_NONE;
            role.componentType = EComponentType::E_COMPONENT_TYPE_UNKNOW;
        }

        mVideoRender.reset();
        mAudioOutput.reset();
        mVideoDecoder.reset();
        mAudioDecoder.reset();
        mSubtitle.reset();
        mSubtitleLooper.reset();
        mExternalCues.clear();
        mNextExternalTrackIndex = kExternalTrackIndexBase;
        mSource.reset();

        mVideoRenderLooper.reset();
        mAudioOutputLooper.reset();
        mVideoDecodeLooper.reset();
        mAudioDecodeLooper.reset();
        mSourceLooper.reset();

        mAVSync.reset();
        {
            // 与 getCurrentPosition/getDuration 的跨线程读互斥
            std::lock_guard<std::mutex> lk(mMutex);
            mMediaClock.reset();
            mMediaInfo.reset();
        }

        mVideoEOS = false;
        mAudioEOS = false;
        if (mPerfStats) {
            // seek 会让队列骤降骤升、解码器追帧，这段的样本与峰值不代表
            // 稳态，留着会污染整段读数
            mPerfStats->reset();
            mTimeline.reset();
        }
        setAllRoles(ROLE_NONE);
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
    }

    VEResult VEPlayer::onReset(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        if (isFlowBusy()) {
            // 排到当前流程完成后执行；若在途的是 seek，请求其尽快中止；
            // 若在途的是 prepare，中断其阻塞 IO 让它尽快失败返回
            mAbortSeek = (mSeekStage != SEEK_STAGE_NONE);
            if (mState == STATE_PREPARING && mSource) {
                mSource->abort();
            }
            dropQueuedSeeks();
            PendingAction action;
            action.type = PendingAction::ACTION_RESET;
            mPendingActions.push_back(action);
            return VE_OK;
        }
        executeReset();
        return VE_OK;
    }

    void VEPlayer::executeReset() {
        // reset 后应能重新 setDataSource：必须真正拆掉这一套组件和线程，
        // 否则再次 setDataSource 会又建一套 looper，线程只增不减。
        teardownComponents([this] {
            mPath.clear();
            mState = STATE_IDLE;
            ALOGI("VEPlayer::onReset done");
            processPendingActions();
        });
    }

    VEResult VEPlayer::onRelease(std::shared_ptr<AMessage> msg) {
        // 预热的 codec 实例若没人来取(非硬解白名单、回退软解、prepare 失败)，
        // 必须在这里丢掉——MediaCodec 实例是有限的系统资源，握着不用会挤掉
        // 其它应用的分配
        VECodecWarmup::discard();
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        // release 是同步调用(Driver 析构在等)，但拆解要跨多轮消息握手，
        // 所以先扣下 replyToken；无论直接执行还是排队，最终都必须回复。
        std::shared_ptr<AReplyToken> replyID;
        bool wantsReply = msg->senderAwaitsResponse(replyID);

        if (isFlowBusy()) {
            // 排到当前流程完成后执行；在途的 seek 请求尽快中止，
            // 保证 onDestroy 的同步 release 不被慢 seek 拖满整个阶段链；
            // 在途的 prepare 中断其阻塞 IO
            mAbortSeek = (mSeekStage != SEEK_STAGE_NONE);
            if (mState == STATE_PREPARING && mSource) {
                mSource->abort();
            }
            dropQueuedSeeks();
            PendingAction action;
            action.type = PendingAction::ACTION_RELEASE;
            action.reply = replyID;
            action.wantsReply = wantsReply;
            mPendingActions.push_back(action);
            return VE_OK;
        }

        executeRelease(replyID, wantsReply);
        return VE_OK;
    }

    void VEPlayer::executeRelease(const std::shared_ptr<AReplyToken> &reply, bool wantsReply) {
        teardownComponents([this, reply, wantsReply] {
            if (mWindow) {
                ANativeWindow_release(mWindow);
                mWindow = nullptr;
            }
            mState = STATE_IDLE;
            ALOGI("VEPlayer::onRelease done");

            if (wantsReply) {
                std::shared_ptr<AMessage> response = std::make_shared<AMessage>();
                response->setInt32("ret", VE_OK);
                response->postReply(reply);
            }
            processPendingActions();
        });
    }

    void VEPlayer::setLooping(bool enable) {
        mEnableLoop = enable;
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
        return info->duration;
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

    void VEPlayer::setOnPreparedListener(funOnPreparedCallback callback) {
        onPreparedCallback = std::move(callback);
    }

    void VEPlayer::setOnSeekComplateListener(funOnSeekComplateCallback callback) {
        onSeekComplateCallback = std::move(callback);
    }

    VEResult VEPlayer::setPlaySpeed(float speed) {
        if (speed < kMinPlaybackSpeed || speed > kMaxPlaybackSpeed) {
            ALOGE("VEPlayer::%s speed %.2f out of range [%.1f, %.1f]", __FUNCTION__,
                  speed, kMinPlaybackSpeed, kMaxPlaybackSpeed);
            return VE_INVALID_PARAMS;
        }
        auto msg = std::make_shared<AMessage>(kWhatSetSpeed, shared_from_this());
        msg->setFloat("speed", speed);
        msg->post();
        return VE_OK;
    }

    VEResult VEPlayer::onSetSpeed(const std::shared_ptr<AMessage> &msg) {
        float speed = 1.0f;
        msg->findFloat("speed", &speed);
        if (isFlowBusy()) {
            // 流程在途：排队，与队尾同类型请求合并(连续拖动速率条时只做最后一次)
            if (!mPendingActions.empty() &&
                mPendingActions.back().type == PendingAction::ACTION_SET_SPEED) {
                mPendingActions.back().speed = speed;
            } else {
                PendingAction action;
                action.type = PendingAction::ACTION_SET_SPEED;
                action.speed = speed;
                mPendingActions.push_back(action);
            }
            return VE_OK;
        }
        return doSetSpeed(speed);
    }

    VEResult VEPlayer::doSetSpeed(float speed) {
        if (speed == mPlaybackSpeed) {
            return VE_OK;
        }
        ALOGI("VEPlayer::%s %.2f -> %.2f", __FUNCTION__, mPlaybackSpeed, speed);
        mPlaybackSpeed = speed;

        // 时钟先按旧速率结算已播时间再改速率(VEMediaClock 内部处理)，
        // 之后视频侧自动跟随：AVSync 拿新速率折算等待时间，落后就丢帧
        double anchorUs = 0;
        if (mMediaClock) {
            anchorUs = mMediaClock->getCurrentMediaTime();
            mMediaClock->setPlaybackSpeed(speed);
        }
        // 音频侧要重建变速器状态并清掉设备里的旧速率 PCM
        if (mAudioOutput) {
            mAudioOutput->setSpeed(speed, anchorUs);
        }
        // 字幕定时器是按旧速率算的真实时长，也要重排
        if (mSubtitle) {
            mSubtitle->setSpeed(speed);
        }
        return VE_OK;
    }

    void VEPlayer::onEOS() {
        if (mSeekStage != SEEK_STAGE_NONE) {
            // seek 过程中管线被 flush，此时的 EOS 不代表播放结束
            ALOGI("VEPlayer::%s ignored during seek", __FUNCTION__);
            return;
        }

        // 只统计实际存在的链路：纯音频/纯视频文件不该等一条永远不会到来的 EOS。
        //
        // 判据必须是"轨道在不在"，不能拿渲染器对象是否为空来代替：硬解路径下
        // 显示端就是解码器本身，mVideoRender 永远是 null，用它判会让带音频的
        // 视频在音频 EOS 一到就判定播完，视频尾巴被截掉。
        const bool needVideoEOS = (mMediaInfo != nullptr) && mMediaInfo->hasVideo();
        const bool needAudioEOS = (mMediaInfo != nullptr) && mMediaInfo->hasAudio();
        bool videoDone = !needVideoEOS || mVideoEOS;
        bool audioDone = !needAudioEOS || mAudioEOS;

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
            // 完成后的轻量收敛：暂停组件与时钟。音频渲染器在 EOS 帧处
            // 已自停设备，这里是统一兜底，保证 COMPLETED 是真正的静止态
            if (mMediaClock) mMediaClock->pause();
            forEachRole([](Role &r) { r.comp->pause(); });
            // 收尾补一次满进度，避免进度条停在最后一次 tick 的位置
            if (mMediaInfo != nullptr && mMediaInfo->duration > 0) {
                notifyProgress(static_cast<int64_t>(mMediaInfo->duration) * 1000);
            }
            if (onCompleteCallback) {
                onCompleteCallback();
            }
        }
    }

    VEResult VEPlayer::onSurfaceChanged(ANativeWindow *win, int viewWidth, int viewHeight) {
        if (mWindow != nullptr && mWindow != win) {
            // JNI 层每次 setSurface 都 acquire 一个新引用，旧引用必须在
            // 覆盖前释放，否则转屏/前后台一次就漏一个 window
            ANativeWindow_release(mWindow);
        }
        mWindow = win;
        mViewWidth = viewWidth;
        mViewHeight = viewHeight;

        // 软解走显示组件；硬解时 surface 绑在 codec 上，要通知解码器换绑
        if (mVideoRender != nullptr) {
            mVideoRender->setSurface(mWindow, mViewWidth, mViewHeight);
        } else if (mVideoHardware && mVideoDecoder != nullptr) {
            auto hw = std::dynamic_pointer_cast<VEMediaCodecVideoDecoder>(mVideoDecoder);
            if (hw) {
                hw->setSurface(mWindow);
            }
        }

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
                int32_t hint = 0;
                msg->findInt32("arg2", &hint);
                if (hint == VE_INFO_DECODER_FALLBACK && mState != STATE_RELEASING &&
                    !mDecoderPolicy.forceSoftware) {
                    // 硬解故障不是播放失败：换软解重建视频链继续播。
                    // 走 PendingAction 串行化，避免与在途的 seek/切轨打架。
                    if (isFlowBusy()) {
                        PendingAction action;
                        action.type = PendingAction::ACTION_REBUILD_VIDEO;
                        mPendingActions.push_back(action);
                    } else {
                        rebuildVideoAsSoftware();
                    }
                    break;
                }
                if (mState == STATE_RELEASING) {
                    // 拆解本身就是最彻底的收敛；此刻改状态会击穿
                    // "RELEASING 期间 defer"的防线，只记日志
                    ALOGW("VEPlayer component error %d during teardown, ignored", code);
                    break;
                }
                // 错误即收敛：不能让数据面自治的组件继续各跑各的
                // (如视频解码器挂了音频还在响)，也不能留下既停不下来
                // 又救不回来的半死状态
                converge();
                mState = STATE_ERROR;
                notifyError(code, "component reported error");
                dropQueuedSeeks();
                processPendingActions();   // 排队的 reset/release 是 ERROR 态的唯一出路
                break;
            }
            case VE_NOTIFY_EVENT_SUBTITLE: {
                std::string text;
                msg->findString("text", text);
                notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE, 0, text, nullptr);
                break;
            }
            case VE_NOTIFY_EVENT_SUBTITLE_CLEAR: {
                notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE_CLEAR, 0, "", nullptr);
                break;
            }
            case VE_NOTIFY_EVENT_SELECT_TRACK_DONE: {
                // demux 侧换流完成；播放器的后续动作(重建/seek)已经在
                // doSelectTrack 里同步发起，这里只记日志
                int32_t ret = 0;
                msg->findInt32("arg1", &ret);
                ALOGI("VEPlayer::%s source select track done, ret=%d", __FUNCTION__, ret);
                break;
            }
            case VE_NOTIFY_EVENT_BUFFERING_START: {
                int32_t percent = 0;
                msg->findInt32("arg1", &percent);
                notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_START, percent,
                           "buffering start", nullptr);
                // 只在正常播放时才真的停数据面：seek/teardown 在途时
                // 它们自己会重建数据流，这里插手只会打架
                if (mState == STATE_STARTED && !isFlowBusy() && !mBuffering) {
                    mBuffering = true;
                    ALOGI("VEPlayer::%s buffering, pausing data flow", __FUNCTION__);
                    stopProgressTick();
                    if (mMediaClock) mMediaClock->pause();
                    // 不动 mState、不动 seek 流程状态：这是内部暂停，
                    // 对外仍然是 STARTED
                    forEachRole([](Role &r) { r.comp->pause(); });
                }
                break;
            }
            case VE_NOTIFY_EVENT_BUFFERING_UPDATE: {
                int32_t percent = 0;
                msg->findInt32("arg1", &percent);
                notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_UPDATE, percent,
                           "buffering update", nullptr);
                break;
            }
            case VE_NOTIFY_EVENT_BUFFERING_END: {
                int32_t percent = 0;
                msg->findInt32("arg1", &percent);
                notifyInfo(0, VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_END, percent,
                           "buffering end", nullptr);
                if (mBuffering) {
                    mBuffering = false;
                    // 缓冲期间用户可能按了暂停：那时 mState 已变，
                    // 不能擅自恢复播放
                    if (mState == STATE_STARTED && !isFlowBusy()) {
                        ALOGI("VEPlayer::%s buffering done, resuming", __FUNCTION__);
                        forEachRole([](Role &r) { r.comp->start(); });
                        if (mMediaClock) mMediaClock->resume();
                        startProgressTick();
                    }
                }
                break;
            }
            case VE_NOTIFY_EVENT_PREPARE_DONE: {
                if (mState != STATE_PREPARING ||
                    type != EComponentType::E_COMPONENT_TYPE_DEMUX) {
                    ALOGW("VEPlayer::%s stray PREPARE_DONE in state %d, ignored",
                          __FUNCTION__, mState);
                    break;
                }
                int32_t ret = VE_UNKNOWN_ERROR;
                msg->findInt32("arg1", &ret);
                if (ret != VE_OK) {
                    converge();
                    mState = STATE_ERROR;
                    notifyError(VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, "demux open failed!!");
                } else {
                    continuePrepare();
                }
                // prepare 流程结束，接力排队的操作
                processPendingActions();
                break;
            }
            case VE_NOTIFY_EVENT_FIRST_FRAME: {
                // 首帧既是 seek 完成的依据，也顺带更新一次进度
                int64_t pts = 0;
                msg->findInt64("arg3", &pts);
                notifyProgress(pts);
                if (mSeekStage == SEEK_STAGE_PRIMING &&
                    type == EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER) {
                    if (mAbortSeek) {
                        if (mSeekTrace) { mSeekTrace->abort(); }
                        abortSeekForAction();
                    } else {
                        // 精度 = 首帧实际 pts − 请求位置，只有这里拿得到
                        if (mSeekTrace) { mSeekTrace->endPriming(pts); }
                        seekFinish();
                    }
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
                    if (mAbortSeek) {
                        abortSeekForAction();
                    } else {
                        seekStageSeek();
                    }
                }
                break;
            }
            case VE_NOTIFY_EVENT_SEEK_DONE: {
                if (mSeekStage == SEEK_STAGE_SEEKING &&
                    acceptAck(type, ROLE_SEEKING, ROLE_SEEK_DONE) &&
                    rolesAllIn(ROLE_SEEK_DONE)) {
                    if (mAbortSeek) {
                        abortSeekForAction();
                    } else {
                        seekStagePrime();
                    }
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

    void VEPlayer::setRole(RoleIndex idx, const std::shared_ptr<IVEComponent> &comp,
                           const std::shared_ptr<ALooper> &looper, int32_t componentType) {
        mRoles[idx].comp = comp;
        mRoles[idx].looper = looper;
        mRoles[idx].state = comp ? ROLE_ACTIVE : ROLE_NONE;
        mRoles[idx].componentType = componentType;
    }

    void VEPlayer::forEachRole(const std::function<void(Role &)> &fn) {
        for (auto &role : mRoles) {
            if (role.comp) {
                fn(role);
            }
        }
    }

    VEPlayer::RoleState *VEPlayer::roleStateFor(int32_t type) {
        // 同一个组件可能占多个槽位(硬解解码器同时是解码与显示)，
        // 因此按 componentType 精确匹配槽位，而不是按对象
        for (auto &role : mRoles) {
            if (role.state != ROLE_NONE && role.componentType == type) {
                return &role.state;
            }
        }
        return nullptr;
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
        for (const auto &role : mRoles) {
            if (role.state != ROLE_NONE && role.state != done) {
                return false;
            }
        }
        return true;
    }

    void VEPlayer::setAllRoles(RoleState s) {
        for (auto &role : mRoles) {
            if (role.state != ROLE_NONE) {
                role.state = s;
            }
        }
    }

    bool VEPlayer::anyRoleExists() const {
        for (const auto &role : mRoles) {
            if (role.state != ROLE_NONE) {
                return true;
            }
        }
        return false;
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
            bool inReleaseStage = rolesAllIn(ROLE_RELEASED);
            for (const auto &role : mRoles) {
                if (role.state == ROLE_RELEASING) {
                    inReleaseStage = true;
                    break;
                }
            }
            if (inReleaseStage) {
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
        converge();
        dropQueuedSeeks();
        mState = STATE_ERROR;
        notifyError(VE_TIMED_OUT, "seek timed out, component not responding");
        // 排队的 reset/release 仍要执行(ERROR 态下它们是唯一出路)
        processPendingActions();
    }

    void VEPlayer::converge() {
        ++mFlowSeq;
        mSeekStage = SEEK_STAGE_NONE;
        mAbortSeek = false;
        mBuffering = false;
        setAllRoles(ROLE_ACTIVE);
        stopProgressTick();
        if (mMediaClock) {
            mMediaClock->pause();
        }
        // fire-and-forget 停掉数据面(组件回的 STOP_DONE 会被角色守卫丢弃)
        forEachRole([](Role &r) { r.comp->stop(); });
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

        // 逐秒时间线。挂在已有的 tick 上而不是另起定时器：多一个周期性
        // 唤醒源就多一次 CPU 唤醒，而这个 tick 本来就在跑
        if (mPerfStats) {
            // 三个深度都可能是 -1(轨不存在/硬解不走 VEVideoDisplay)，
            // 原样透传给时间线，**不要折成 0**：无音轨的片子报 aq=0
            // 看起来就是"音频缓冲空了"，那是完全不同的结论
            const int aq = mSource ? mSource->getQueueDepth(ETrackType::AUDIO) : -1;
            const int vq = mSource ? mSource->getQueueDepth(ETrackType::VIDEO) : -1;
            const int fq = mVideoRender ? mVideoRender->getFrameQueueDepth() : -1;
            // A/V 偏移要求**两条轨都在**(aq/vq<0 即该轨不存在, 见
            // VESource::getQueueDepth)。缺任一条时它都不是偏移:
            //   无视频 → lastDiff 从未被更新, 随时钟单调发散(实测每秒 1000ms)
            //   无音频 → 拿视频跟一个不存在的音频时钟比, 数字有但没有含义
            // 两种情况都传哨兵
            const bool avComparable = (aq >= 0 && vq >= 0);
            const int64_t avOffUs = (mAVSync && avComparable)
                                    ? mAVSync->getLastDiffUs()
                                    : VEPerfStats::kNoSyncSample;
            mTimeline.maybeEmit(*mPerfStats, aq, vq, fq, avOffUs);
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

        if (isFlowBusy()) {
            // 流程在途：入队并与队尾 SEEK 合并——只保留最后一次请求，
            // 完成后补做(拖动进度条的关键)
            ALOGI("VEPlayer::startSeek queue pending seek to %f", timestampMs);
            if (!mPendingActions.empty() &&
                mPendingActions.back().type == PendingAction::ACTION_SEEK) {
                mPendingActions.back().seekMs = timestampMs;
            } else {
                PendingAction action;
                action.type = PendingAction::ACTION_SEEK;
                action.seekMs = timestampMs;
                mPendingActions.push_back(action);
            }
            return;
        }

        ALOGI("VEPlayer::startSeek to %f ms", timestampMs);
        if (mSeekTrace == nullptr) {
            mSeekTrace = std::make_shared<VESeekTrace>();
        }
        // 打在这里而不是函数入口：上面"流程在途则排队合并"那条路径不算一次
        // 真正的 seek，记进去会把排队等待算成 seek 耗时
        mSeekTrace->begin(timestampMs, mVideoHardware);
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

        forEachRole([](Role &r) { r.comp->pause(); r.state = ROLE_PAUSING; });
        postFlowTimeout(kAckTimeoutUs);
    }

    void VEPlayer::seekStageSeek() {
        // ② demux 定位到目标关键帧，解码器 flush 并记下精准 seek 目标
        if (mSeekTrace) { mSeekTrace->endPausing(); }
        ALOGI("VEPlayer::seek stage 2/3 - seeking demux & flushing decoders");
        mSeekStage = SEEK_STAGE_SEEKING;
        ++mFlowSeq;

        const double target = mSeekTargetMs;
        forEachRole([target](Role &r) { r.comp->seekTo(target); r.state = ROLE_SEEKING; });
        postFlowTimeout(kAckTimeoutUs);
    }

    void VEPlayer::seekStagePrime() {
        // ③ 把时钟重新定位到目标位置后重启管线，等第一帧真正上屏
        if (mSeekTrace) { mSeekTrace->endSeeking(); }
        ALOGI("VEPlayer::seek stage 3/3 - priming first frame");
        mSeekStage = SEEK_STAGE_PRIMING;
        ++mFlowSeq;
        // 本轮 seek 的命令-回执循环到此为止，角色收拢回稳态；
        // 阶段③只等视频显示的 FIRST_FRAME 事件(由 mSeekStage 守卫)
        setAllRoles(ROLE_ACTIVE);

        if (mMediaClock) {
            // 暂停态 seek 保持时钟冻结：否则等首帧期间时钟偷跑，
            // 最终位置=目标+首帧耗时，暂停拖动会看到画面"动一下"
            mMediaClock->resetTo(mSeekTargetMs * 1000.0,
                                 mStateBeforeSeek != STATE_STARTED);
        }
        if (mAVSync) {
            mAVSync->reset(mSeekTargetMs * 1000.0);
        }

        if (mVideoRender && mWindow != nullptr) {
            // 有视频且 surface 就绪时以首帧上屏作为 seek 完成的判据；
            // 暂停态下也要出这一帧，否则 seek 后画面不会更新。
            // 无 surface 时永远等不到首帧，直接走完成分支(E2)。
            if (mSource)        mSource->start();
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
                if (mSource)        mSource->start();
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
            forEachRole([](Role &r) { r.comp->start(); });
            if (mMediaClock) {
                mMediaClock->resume();
            }
            // 进度 tick 在 seek 期间(状态为 SEEKING)已经自行中断，这里必须重新拉起
            startProgressTick();
        } else {
            // 暂停态 seek：预览帧已经上屏，重新回到暂停
            mState = STATE_PAUSED;
            forEachRole([](Role &r) { r.comp->pause(); });
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

        mAbortSeek = false;
        // 排队等待的操作(合并后的 seek/reset/release)现在补做
        processPendingActions();
    }
}