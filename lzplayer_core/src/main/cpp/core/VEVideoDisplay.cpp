//
// Created by 李振 on 2025/7/24.
//

#include "VEVideoDisplay.h"
#include "VEVideoDecoder.h"
#include "VEGLESVideoRenderer.h"
#include "VEDef.h"

namespace VE {
    VEVideoDisplay::VEVideoDisplay(const std::shared_ptr<AMessage> &notify,
                                   const std::shared_ptr<VEAVsync> &avSync):m_pNotify(notify),m_pAvSync(avSync) {
        ALOGI("VEVideoDisplay construct");
    }

    VEVideoDisplay::~VEVideoDisplay() {

    }

    VEResult VEVideoDisplay::prepare(VEBundle params) {
        ALOGV("VEVideoDisplay::%s enter",__FUNCTION__ );
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        ANativeWindow *win = params.get<ANativeWindow *>("surface");
        msg->setPointer("win", win);
        msg->setInt32("width", params.get<int>("width"));
        msg->setInt32("height", params.get<int>("height"));
        msg->setInt32("fps", params.get<int>("fps"));
        msg->setObject("vdec", params.get<std::shared_ptr<IMediaDecoder>>("decoder"));
        msg->post();
        return 0;
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
        // 先绑定解码器和尺寸再看 surface：以前 surface 为空直接 return，
        // m_pVideoDec 没赋值，之后 start 就是空指针崩溃，且渲染器
        // 永远建不起来(onSurfaceChanged 只在渲染器已存在时才处理)。
        std::shared_ptr<void> tmp = nullptr;
        msg->findObject("vdec", &tmp);
        m_pVideoDec = std::static_pointer_cast<IMediaDecoder>(tmp);
        msg->findPointer("win", (void **) &mWin);
        msg->findInt32("width", &mViewWidth);
        msg->findInt32("height", &mViewHeight);

        if(mWin == nullptr){
            // surface 还没设置：渲染器延迟到 setSurface 时创建
            ALOGW("VEVideoDisplay::onPrepare no surface yet, defer renderer init");
            return VE_OK;
        }

        VEBundle params;
        params.set("surface",mWin);
        params.set("width",mViewWidth);
        params.set("height",mViewHeight);
        m_pVideoRender = std::make_shared<VEGLESVideoRenderer>();
        m_pVideoRender->initialize(params);
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
        return 0;
    }

    VEResult VEVideoDisplay::onSeekTo(double timestampMs) {
        ALOGV("VEVideoDisplay::onSeekTo enter timestampMs:%f", timestampMs);
        // 作废在途的渲染/同步消息，避免 seek 后把旧帧画上去
        ++m_Epoch;
        m_IsStarted = false;
        // 标记：seek 后渲染出的第一帧要上报，作为 seek 真正完成的依据
        m_NotifyFirstFrame = true;
        return VE_OK;
    }

    VEResult VEVideoDisplay::onFlush(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::onFlush enter");
        ++m_Epoch;
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
        m_pVideoDec.reset();
        mWin = nullptr;
        return VE_OK;
    }

    VEResult VEVideoDisplay::onRender(std::shared_ptr<AMessage> msg) {
        ALOGV("VEVideoDisplay::%s enter", __FUNCTION__);
        if (!m_IsStarted) {
            ALOGW("VEVideoDisplay::%s - render not started", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        std::shared_ptr<VEFrame> frame = nullptr;
        std::shared_ptr<void> tmp;

        msg->findObject("render", &tmp);

        frame = std::static_pointer_cast<VEFrame>(tmp);

        if (frame == nullptr || frame->getFrame() == nullptr) {
            ALOGE("VEVideoDisplay::%s - frame or frame data is null", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        if (m_pVideoRender == nullptr) {
            // surface 尚未就绪，丢掉这一帧；渲染链由 onSurfaceChanged 重新拉起
            ALOGW("VEVideoDisplay::%s - renderer not ready, drop frame", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        mFrameWidth = frame->getFrame()->width;
        mFrameHeight = frame->getFrame()->height;


        m_pVideoRender->renderFrame(frame);

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

        std::shared_ptr<VEFrame> frame = nullptr;
        if (m_pVideoDec == nullptr) {
            ALOGE("VEVideoDisplay::%s - decoder not set", __FUNCTION__);
            return UNKNOWN_ERROR;
        }
        VEResult ret = m_pVideoDec->readFrame(frame);
        ALOGV("VEVideoDisplay::%s readFrame result: %d", __FUNCTION__, ret);

        if (ret == VE_NOT_ENOUGH_DATA) {
            ALOGV("VEVideoDisplay::%s needMoreFrame!!!", __FUNCTION__);
            auto wakeMsg = std::make_shared<AMessage>(kWhatSync, shared_from_this());
            wakeMsg->setInt32("epoch", m_Epoch);
            m_pVideoDec->needMoreFrame(wakeMsg);
            return VE_NOT_ENOUGH_DATA;
        }

        if (frame == nullptr) {
            ALOGE("VEVideoDisplay::%s onRender read frame is null!!!", __FUNCTION__);
            return UNKNOWN_ERROR;
        }
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
            return UNKNOWN_ERROR;
        }

        m_pAvSync->updateVideoPts(frame->getPts());

        if (m_pAvSync->shouldDropFrame()) {
            // 已经严重落后：直接丢弃，不投递渲染消息也不等待，立即取下一帧追赶
            ALOGI("VEVideoDisplay::%s Dropping frame pts=%" PRId64, __FUNCTION__, frame->getPts());
            postSync(0);
            return VE_OK;
        }

        int64_t waitTime = m_pAvSync->getWaitTime(); // 获取等待时间
        ALOGD("VEVideoDisplay::%s waitTime:%" PRId64, __FUNCTION__, waitTime);
        std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatRender,
                                                                         shared_from_this());
        renderMsg->setObject("render", frame);
        renderMsg->setInt32("epoch", m_Epoch);
        renderMsg->post(waitTime);
        return VE_OK;
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
            m_pVideoRender = std::make_shared<VEGLESVideoRenderer>();
            m_pVideoRender->initialize(params);
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