//
// Created by 李振 on 2025/7/24.
//

#include "VEVideoDisplay.h"
#include "VEVideoDecoder.h"
#include "VEGLESVideoRenderer.h"

namespace VE {
    VEVideoDisplay::VEVideoDisplay(const std::shared_ptr<AMessage> &notify,
                                   const std::shared_ptr<VEAVsync> &avSync):m_pNotify(notify),m_pAvSync(avSync) {
        ALOGI("VEVideoDisplay construct");
    }

    VEVideoDisplay::~VEVideoDisplay() {

    }

    VEResult VEVideoDisplay::prepare(VEBundle params) {
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        ANativeWindow *win = params.get<ANativeWindow *>("surface");
        msg->setPointer("win", win);
        msg->setInt32("width", params.get<int>("width"));
        msg->setInt32("height", params.get<int>("height"));
        msg->setInt32("fps", params.get<int>("fps"));
        msg->setObject("vdec", params.get<std::shared_ptr<VEVideoDecoder>>("decoder"));
        msg->post();
        return 0;
    }

    VEResult VEVideoDisplay::start() {
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
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
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::stop - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return 0;
    }

    VEResult VEVideoDisplay::seekTo(double timestamp) {
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
        try {
            std::make_shared<AMessage>(kWhatSeek, shared_from_this())->post();
            return 0;
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::pause - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return 0;
    }

    VEResult VEVideoDisplay::flush() {
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
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
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
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
        ALOGI("VEVideoDisplay::%s enter",__FUNCTION__ );
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
                break;
            }
            case kWhatFlush:{
                onFlush(msg);
                break;
            }
            case kWhatStop:{
                onStop(msg);
                break;
            }
            case kWhatRender:{
                if (onRender(msg) == VE_OK) {
                    std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatSync,
                                                                                     shared_from_this());
                    renderMsg->post();
                }
                break;
            }
            case kWhatSurfaceChanged:{
                onSurfaceChanged(msg);
                break;
            }
            case kWhatSync:{
                onAVSync(msg);
                break;
            }
            case kWhatSpeedRate:{

                break;
            }
            case kWhatSeek:{
                onSeekTo(0);
                break;
            }
            default:{
                break;
            }
        }
    }

    VEResult VEVideoDisplay::onPrepare(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoDisplay::onPrepare enter");
        msg->findPointer("win", (void **) &mWin);

        if(mWin == nullptr){
            ALOGE("VEVideoDisplay::onPrepare invalid surface");
            return VE_INVALID_PARAMS;
        }
        std::shared_ptr<void> tmp = nullptr;

        msg->findObject("vdec", &tmp);
        m_pVideoDec = std::static_pointer_cast<VEVideoDecoder>(tmp);
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
        ALOGI("VEVideoDisplay::onStart enter");
        m_IsStarted = true;
        std::make_shared<AMessage>(kWhatSync, shared_from_this())->post();
        return 0;
    }

    VEResult VEVideoDisplay::onStop(std::shared_ptr<AMessage> msg) {
        m_IsStarted = false;
        return 0;
    }

    VEResult VEVideoDisplay::onSeekTo(double timestamp) {
        return 0;
    }

    VEResult VEVideoDisplay::onFlush(std::shared_ptr<AMessage> msg) {
        return 0;
    }

    VEResult VEVideoDisplay::onPause(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoDisplay::onPause enter");
        m_IsStarted = false;
        return 0;
    }

    VEResult VEVideoDisplay::onRelease(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoDisplay::onRelease enter");
        m_IsStarted = false;
        return 0;
    }

    VEResult VEVideoDisplay::onRender(std::shared_ptr<AMessage> msg) {
        ALOGI("VEVideoDisplay::%s enter", __FUNCTION__);
        if (!m_IsStarted) {
            ALOGW("VEVideoDisplay::%s - render not started", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        int32_t isDrop = false;
        msg->findInt32("drop", &isDrop);

        if (isDrop) {
            ALOGD("VEVideoDisplay::%s - dropping frame", __FUNCTION__);
            return VE_OK;
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

        if (m_pNotify) {
            std::shared_ptr<AMessage> msg = m_pNotify->dup();
            msg->setInt32("type", kWhatProgress);
            msg->setInt64("progress", static_cast<int64_t>(frame->getPts()));
            msg->post();
            ALOGI("VEGLESVideoRenderer::renderFrame - Notifying progress: %" PRId64, frame->getPts());
        }
        return VE_OK;
    }

    VEResult VEVideoDisplay::onAVSync(std::shared_ptr<AMessage> msg) {

        ALOGI("VEVideoDisplay::%s enter", __FUNCTION__);
        if (!m_IsStarted) {
            ALOGE("VEVideoDisplay::%s render not started, mIsStarted=%d", __FUNCTION__, m_IsStarted);
            return UNKNOWN_ERROR;
        }

        bool isDrop = false;
        std::shared_ptr<VEFrame> frame = nullptr;
        VEResult ret = m_pVideoDec->readFrame(frame);
        ALOGI("VEVideoDisplay::%s readFrame result: %d", __FUNCTION__, ret);

        if (ret == VE_NOT_ENOUGH_DATA) {
            ALOGI("VEVideoDisplay::%s needMoreFrame!!!", __FUNCTION__);
            try {
                m_pVideoDec->needMoreFrame(std::make_shared<AMessage>(kWhatSync, shared_from_this()));
            } catch (const std::bad_weak_ptr &e) {
                ALOGE("VEVideoDisplay::onAVSync - Object not managed by shared_ptr yet");
                return UNKNOWN_ERROR;
            }
            return VE_NOT_ENOUGH_DATA;
        }

        if (frame == nullptr) {
            ALOGE("VEVideoDisplay::%s onRender read frame is null!!!", __FUNCTION__);
            return UNKNOWN_ERROR;
        }
        ALOGI("VEVideoDisplay::onAVSync frame type: %d, pts: %" PRId64, frame->getFrameType(),
              frame->getPts());

        if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
            ALOGD("VEVideoDisplay::onAVSync E_FRAME_TYPE_EOF");
            std::shared_ptr<AMessage> eosMsg = m_pNotify->dup();
            eosMsg->setInt32("type", kWhatEOS);
            eosMsg->post();
            return UNKNOWN_ERROR;
        }

        m_pAvSync->updateVideoPts(frame->getPts());

        if (m_pAvSync->shouldDropFrame()) {
            ALOGI("VEVideoDisplay::%s Dropping frame due to sync issues", __FUNCTION__);
            isDrop = true; // 丢帧
        }

        int64_t waitTime = m_pAvSync->getWaitTime(); // 获取等待时间
        ALOGD("VEVideoDisplay::%s waitTime:%" PRId64, __FUNCTION__, waitTime);
        try {
            std::shared_ptr<AMessage> renderMsg = std::make_shared<AMessage>(kWhatRender,
                                                                             shared_from_this());
            renderMsg->setObject("render", frame);
            renderMsg->setInt32("drop", isDrop);
            renderMsg->post(isDrop ? 0 : waitTime); // 根据同步状态设置等待时间
        } catch (const std::bad_weak_ptr &e) {
            ALOGE("VEVideoDisplay::onAVSync - Object not managed by shared_ptr yet");
            return UNKNOWN_ERROR;
        }
        return VE_OK;
        return 0;
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
        ALOGI("VEVideoDisplay::setSurface enter");
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


} // VE