//
// Created by 李振 on 2025/7/24.
//

#include "VEVideoDisplay.h"
#include "VEVideoDecoder.h"
#include "VEVideoRenderFactory.h"
#include "VEDef.h"

namespace VE {
    namespace {
        /// mFrames 深度兜底。正常由解码器 credit(=FRAME_QUEUE_MAX_SIZE)封顶，
        /// 这里只防异常多发回执导致的无界增长：超阈值丢最旧帧并照常回执还 credit。
        constexpr size_t kMaxFramesBackstop = 12;
    }
    VEVideoDisplay::VEVideoDisplay(const std::shared_ptr<AMessage> &notify,
                                   const std::shared_ptr<VEAVsync> &avSync):m_pNotify(notify),m_pAvSync(avSync) {
        ALOGI("VEVideoDisplay construct");
    }

    VEVideoDisplay::~VEVideoDisplay() {

    }

    VEResult VEVideoDisplay::prepare(ANativeWindow *win, int width, int height, int fps,
                                     int rotationDegrees, int frameWidth, int frameHeight) {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setPointer("win", win);
        msg->setInt32("width", width);
        msg->setInt32("height", height);
        msg->setInt32("fps", fps);
        msg->setInt32("rotation", rotationDegrees);
        msg->setInt32("frameWidth", frameWidth);
        msg->setInt32("frameHeight", frameHeight);
        msg->post();
        return 0;
    }

    void VEVideoDisplay::queueFrame(const std::shared_ptr<VEFrame> &frame,
                                    const std::shared_ptr<AMessage> &consumedReply) {
        // 解码器线程调用：转投自己的 looper。队列代次在此刻盖章，
        // flush/seek 之后在途的旧帧会在接收侧被丢弃。
        auto msg = std::make_shared<AMessage>(kWhatQueueFrame, shared_from_this());
        msg->setObject("frame", frame);
        msg->setObject("reply", consumedReply);
        msg->setInt32("queueGen", mQueueGen.load());
        msg->post();
    }

    VEResult VEVideoDisplay::start() {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::start - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return 0;
    }

    VEResult VEVideoDisplay::stop() {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::stop - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    VEResult VEVideoDisplay::seekTo(double timestampMs) {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp", timestampMs);
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDisplay::flush() {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatFlush, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::pause - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return 0;
    }

    VEResult VEVideoDisplay::pause() {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::pause - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return 0;
    }

    VEResult VEVideoDisplay::release() {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatRelease, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::release - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return 0;
    }

    void VEVideoDisplay::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        switch (msg->what()) {
            case kWhatPrepare:{
                onPrepare(msg);
                break;
            }
            case kWhatStart:{
                onStart(msg);
                break;
            }
            case kWhatPause:{
                onPause(msg);
                postMessage(VE_NOTIFY_EVENT_PAUSE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatFlush:{
                onFlush(msg);
                postMessage(VE_NOTIFY_EVENT_FLUSH_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatStop:{
                onStop(msg);
                postMessage(VE_NOTIFY_EVENT_STOP_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatRender:{
                if (isStaleMessage(msg)) {
                    break;
                }
                if (onRender(msg) == VE_OK) {
                    postSync(0);
                }
                break;
            }
            case kWhatSurfaceChanged:{
                onSurfaceChanged(msg);
                break;
            }
            case kWhatSync:{
                if (isStaleMessage(msg)) {
                    break;
                }
                onAVSync(msg);
                break;
            }
            case kWhatQueueFrame:{
                int32_t gen = 0;
                msg->findInt32("queueGen", &gen);
                if (gen != mQueueGen.load()) {
                    // flush/seek 之前在途的旧帧：丢弃。不回执——
                    // 解码器 flush 时已把 credit 清算归零
                    ALOGI("VEVideoDisplay stale queued frame gen=%d cur=%d",
                          gen, mQueueGen.load());
                    break;
                }
                std::shared_ptr<void> f, r;
                msg->findObject("frame", &f);
                msg->findObject("reply", &r);
                // 深度兜底：异常多发回执下 credit 单一防线失守时，丢最旧帧
                // 并照常回执还 credit，避免 mFrames 无界增长耗尽内存
                if (mFrames.size() >= kMaxFramesBackstop) {
                    ALOGW("VEVideoDisplay::onQueueFrame frames overflow %zu, drop oldest",
                          mFrames.size());
                    if (mFrames.front().second) {
                        mFrames.front().second->post();
                    }
                    mFrames.pop_front();
                }
                mFrames.emplace_back(std::static_pointer_cast<VEFrame>(f),
                                     std::static_pointer_cast<AMessage>(r));
                if (mPerfStats) {
                    VEPerfStats::bumpPeak(mPerfStats->frameQueuePeak,
                                          static_cast<int>(mFrames.size()));
                }
                if (m_IsStarted && mFrames.size() == 1) {
                    // 队列从空转非空：链条此前已停摆，帧到达即重新拉起。
                    // 链条活着时队列必然非空，不会重复踢
                    postSync(0);
                }
                break;
            }
            case kWhatSpeedRate:{

                break;
            }
            case kWhatSeek:{
                double timestampMs = 0;
                msg->findDouble("timestamp", &timestampMs);
                onSeekTo(timestampMs);
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatRelease:{
                onRelease(msg);
                postMessage(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
                break;
            }
            default:{
                break;
            }
        }
    }

    VEResult VEVideoDisplay::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::onPrepare enter");
        msg->findPointer("win", (void **) &mWin);
        msg->findInt32("width", &mViewWidth);
        msg->findInt32("height", &mViewHeight);
        msg->findInt32("rotation", &mRotationDegrees);
        msg->findInt32("frameWidth", &mDeclaredFrameWidth);
        msg->findInt32("frameHeight", &mDeclaredFrameHeight);

        if(mWin == nullptr){
            // surface 还没设置：渲染器延迟到 setSurface 时创建
            ALOGW("VEVideoDisplay::onPrepare no surface yet, defer renderer init");
            return VE_OK;
        }

        VEBundle params;
        params.set("surface",mWin);
        params.set("width",mViewWidth);
        params.set("height",mViewHeight);
        params.set("rotation",mRotationDegrees);
        params.set("frameWidth", mDeclaredFrameWidth);
        params.set("frameHeight", mDeclaredFrameHeight);
        m_pVideoRender = VEVideoRenderFactory::create(params, mRenderPolicy, &mUsedVulkan);
        if (m_pVideoRender) {
            m_pVideoRender->setPerfStats(mPerfStats);
        }
        return 0;
    }

    VEResult VEVideoDisplay::onStart(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::onStart enter");
        if (m_IsStarted) {
            return VE_OK;
        }
        m_IsStarted = true;
        postSync(0);
        return VE_OK;
    }

    VEResult VEVideoDisplay::onStop(std::shared_ptr<AMessage> msg) {
        m_IsStarted = false;
        ++mQueueGen;
        mFrames.clear();
        return 0;
    }

    VEResult VEVideoDisplay::onSeekTo(double timestampMs) {
        ALOGV("VEVideoDisplay::onSeekTo enter timestampMs:%f", timestampMs);
        // 作废在途的渲染/同步消息，避免 seek 后把旧帧画上去
        ++m_Epoch;
        // 队列代次递增 + 清本地队列：在途的旧帧到达时被丢弃。
        // 不为被清帧发回执——解码器同轮 flush 会把 credit 清算归零
        ++mQueueGen;
        mFrames.clear();
        m_IsStarted = false;
        // 标记：seek 后渲染出的第一帧要上报，作为 seek 真正完成的依据
        m_NotifyFirstFrame = true;
        return VE_OK;
    }

    VEResult VEVideoDisplay::onFlush(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::onFlush enter");
        ++m_Epoch;
        ++mQueueGen;
        mFrames.clear();
        m_IsStarted = false;
        return VE_OK;
    }

    bool VEVideoDisplay::isStaleMessage(const std::shared_ptr<AMessage> &msg) const {
        int32_t epoch = 0;
        msg->findInt32("epoch", &epoch);
        if (epoch != m_Epoch) {
            ALOGI("VEVideoDisplay stale msg epoch=%d cur=%d", epoch, m_Epoch);
            return true;
        }
        return false;
    }

    void VEVideoDisplay::postSync(int64_t delayUs) {
        auto syncMsg = std::make_shared<AMessage>(kWhatSync, shared_from_this());
        syncMsg->setInt32("epoch", m_Epoch);
        syncMsg->post(delayUs);
    }

    VEResult VEVideoDisplay::onPause(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::onPause enter");
        m_IsStarted = false;
        return 0;
    }

    VEResult VEVideoDisplay::onRelease(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::onRelease enter");
        m_IsStarted = false;
        ++m_Epoch;
        if (m_pVideoRender) {
            // 必须在渲染线程上销毁 EGL 环境
            m_pVideoRender->uninitialize();
            m_pVideoRender.reset();
        }
        ++mQueueGen;
        mFrames.clear();
        mWin = nullptr;
        return VE_OK;
    }

    VEResult VEVideoDisplay::onRender(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::%s enter", __FUNCTION__);
        if (!m_IsStarted) {
            ALOGW("VEVideoDisplay::%s - render not started", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        if (mFrames.empty()) {
            ALOGW("VEVideoDisplay::%s - queue drained before render", __FUNCTION__);
            return UNKNOWN_ERROR;
        }
        std::shared_ptr<VEFrame> frame = mFrames.front().first;

        if (frame == nullptr || frame->getFrame() == nullptr) {
            ALOGE("VEVideoDisplay::%s - frame or frame data is null", __FUNCTION__);
            consumeFront();
            return UNKNOWN_ERROR;
        }

        if (m_pVideoRender == nullptr) {
            // surface 尚未就绪，丢掉这一帧(回执照发)；渲染链由 onSurfaceChanged 重新拉起
            ALOGW("VEVideoDisplay::%s - renderer not ready, drop frame", __FUNCTION__);
            consumeFront();
            return UNKNOWN_ERROR;
        }

        mFrameWidth = frame->getFrame()->width;
        mFrameHeight = frame->getFrame()->height;

        // 同步余量取在提交**之前**：正=帧提前就绪(健康)，负=已迟到。
        // 用 AVSync 现成的 getLastDiffUs()，不再自己重算一遍 pts−clock
        // (getWaitTime/getLastDiffUs/shouldDropFrame 已经各算过一次了)
        if (mPerfStats && m_pAvSync) {
            mPerfStats->syncMarginUs.add(m_pAvSync->getLastDiffUs());
        }
        const int64_t presentBeginUs = mPerfStats ? nowUs() : 0;
        m_pVideoRender->renderFrame(frame);
        if (mPerfStats) {
            mPerfStats->presentUs.add(nowUs() - presentBeginUs);
        }
        ++mRenderedFrames;
        // T7：软解路径的"首帧上屏"。**不能复用 FIRST_FRAME 事件**——
        // 那个事件只在 seek 路径发(m_NotifyFirstFrame 仅由 onSeekTo 置真)，
        // 起播时根本不会走到，而且它已经透到 Java 层，改语义会影响 app 行为。
        // mark() 自带"首次生效"语义，所以这里无条件调用即可。
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T7_FIRST_FRAME_PRESENTED);
        }
        consumeFront();

        if (m_NotifyFirstFrame) {
            // seek 后的首帧已经上屏，此时才算 seek 真正完成
            m_NotifyFirstFrame = false;
            postMessage(VE_NOTIFY_EVENT_FIRST_FRAME, 0, 0,
                        static_cast<int64_t>(frame->getPts()), nullptr);
        }
        // 进度不再逐帧上报：由播放器按固定间隔读主时钟统一上报，
        // 这样纯音频文件也有进度，且省掉每秒几十条跨线程消息 + JNI 回调
        return VE_OK;
    }

    VEResult VEVideoDisplay::onAVSync(std::shared_ptr<AMessage> msg) {

        ALOGV("VEVideoDisplay::%s enter", __FUNCTION__);
        if (!m_IsStarted) {
            ALOGE("VEVideoDisplay::%s render not started, mIsStarted=%d", __FUNCTION__, m_IsStarted);
            return UNKNOWN_ERROR;
        }

        if (mFrames.empty()) {
            // 队列空 ⟺ 渲染链停摆。不需要任何唤醒登记：
            // 下一帧到达(onQueueFrame)会重新拉起链条
            ALOGV("VEVideoDisplay::%s queue empty, parking", __FUNCTION__);
            return VE_NOT_ENOUGH_DATA;
        }

        const std::shared_ptr<VEFrame> &frame = mFrames.front().first;
        ALOGV("VEVideoDisplay::onAVSync frame type: %d, pts: %" PRId64, frame->getFrameType(),
              frame->getPts());

        if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
            ALOGD("VEVideoDisplay::onAVSync E_FRAME_TYPE_EOF");
            if (m_NotifyFirstFrame) {
                // seek 目标在最后一帧之后：精准丢帧把可上屏的帧全丢光了，
                // 用 EOF 顶替首帧回执，否则 seek 只能干等 2s 超时兜底
                m_NotifyFirstFrame = false;
                postMessage(VE_NOTIFY_EVENT_FIRST_FRAME, 0, 0, 0, nullptr);
            }
            postMessage(VE_NOTIFY_EVENT_EOS,0,0,0, nullptr);
            consumeFront();   // EOF 哨兵也要回执还 credit
            return UNKNOWN_ERROR;
        }

        m_pAvSync->updateVideoPts(frame->getPts());

        if (m_pAvSync->shouldDropFrame()) {
            // 已经严重落后：丢弃(照样回执还 credit)，立即取下一帧追赶
            ALOGI("VEVideoDisplay::%s Dropping frame pts=%" PRId64, __FUNCTION__, frame->getPts());
            ++mDroppedFrames;
            consumeFront();
            postSync(0);
            return VE_OK;
        }

        int64_t waitTime = m_pAvSync->getWaitTime(); // 获取等待时间
        ALOGV("VEVideoDisplay::%s waitTime:%" PRId64, __FUNCTION__, waitTime);
        // 渲染消息不携带帧：到点后渲染当前队首(队列只在本 looper 上变化)
        std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatRender,
                                                                         shared_from_this());
        renderMsg->setInt32("epoch", m_Epoch);
        renderMsg->post(waitTime);
        return VE_OK;
    }

    void VEVideoDisplay::consumeFront() {
        if (mFrames.empty()) {
            return;
        }
        // 无论渲染还是丢弃，帧被消费就要把回执投回解码器归还 credit
        if (mFrames.front().second) {
            mFrames.front().second->post();
        }
        mFrames.pop_front();
    }

    VEResult VEVideoDisplay::onSurfaceChanged(std::shared_ptr<AMessage> msg) {
        ANativeWindow *newWin = nullptr;
        msg->findPointer("win", (void **) &newWin);
        int newWidth = 0, newHeight = 0;
        msg->findInt32("width", &newWidth);
        msg->findInt32("height", &newHeight);

        ALOGI("VEVideoDisplay::onSurfaceChanged - new surface: %p, size: %dx%d", (void*)newWin, newWidth,
              newHeight);
        mWin = newWin;
        mViewWidth = newWidth;
        mViewHeight = newHeight;

        if (m_pVideoRender != nullptr) {
            if (newWin == nullptr) {
                // surface 销毁：先停渲染链再让渲染器释放 EGLSurface，
                // 否则会持续画向已失效的窗口
                m_SurfaceLost = m_IsStarted;
                m_IsStarted = false;
            }
            m_pVideoRender->changeSurface(newWin, newWidth, newHeight);
            if (newWin != nullptr && m_SurfaceLost) {
                // 之前因 surface 丢失被迫停下，新 surface 就位后复活
                m_SurfaceLost = false;
                m_IsStarted = true;
                postSync(0);
            }
        } else if (newWin != nullptr) {
            // prepare 时 surface 还没就绪，渲染器延迟到此刻创建
            VEBundle params;
            params.set("surface", mWin);
            params.set("width", mViewWidth);
            params.set("height", mViewHeight);
            params.set("rotation", mRotationDegrees);
            params.set("frameWidth", mDeclaredFrameWidth);
            params.set("frameHeight", mDeclaredFrameHeight);
            m_pVideoRender = VEVideoRenderFactory::create(params, mRenderPolicy, &mUsedVulkan);
            if (m_pVideoRender) {
                m_pVideoRender->setPerfStats(mPerfStats);
            }
            if (m_IsStarted) {
                // 渲染链可能因没有渲染器早已断掉，surface 到位后重新拉起
                postSync(0);
            }
        }
        return 0;
    }

    VEResult VEVideoDisplay::setSurface(ANativeWindow *win, int width, int height) {
        // 检查对象是否已经被shared_ptr管理
        ALOGV("VEVideoDisplay::setSurface enter");
        try {
            std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSurfaceChanged,
                                                                       shared_from_this());
            msg->setPointer("win", win);
            msg->setInt32("width", width);
            msg->setInt32("height", height);
            msg->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::setSurface - Object not managed by shared_ptr yet, storing surface info for later");
            // 如果shared_from_this失败，说明对象还没有被shared_ptr管理
            // 直接更新成员变量，等待后续处理
            mWin = win;
            mViewWidth = width;
            mViewHeight = height;
            return 0;
        }
    }

    VEResult VEVideoDisplay::postMessage(int32_t event, int32_t arg1, int32_t arg2,int64_t arg3, void *params) {
        std::shared_ptr<AMessage> msg = m_pNotify->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return 0;
    }


} // VE