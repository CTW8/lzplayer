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

        if(mWin == nullptr){
            ALOGE("VEVideoDisplay::onPrepare invalid surface");
            return VE_INVALID_PARAMS;
        }
        std::shared_ptr<void> tmp = nullptr;

        msg->findObject("vdec", &tmp);
        m_pVideoDec = std::static_pointer_cast<IMediaDecoder>(tmp);
        msg->findInt32("width", &mViewWidth);
        msg->findInt32("height", &mViewHeight);

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
//            std::shared_ptr<AMessage> eosMsg = m_pNotify->dup();
//            eosMsg->setInt32("type", kWhatEOS);
//            eosMsg->post();
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
        if(m_pVideoRender != nullptr){
            ANativeWindow *newWin;
            msg->findPointer("win", (void **) &newWin);
            int newWidth, newHeight;
            msg->findInt32("width", &newWidth);
            msg->findInt32("height", &newHeight);

            ALOGI("VEVideoDisplay::onSurfaceChanged - new surface: %p, size: %dx%d", (void*)newWin, newWidth,
                  newHeight);

            m_pVideoRender->changeSurface(newWin,newWidth,newHeight);
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