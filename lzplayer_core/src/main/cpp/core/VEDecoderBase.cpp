//
// VEDecoderBase.cpp
// NuPlayer-style DecoderBase implementation
// Reference: frameworks/av/media/libmediaplayerservice/nuplayer/NuPlayerDecoderBase.cpp
//

#include "VEDecoderBase.h"
#include "Log.h"

namespace VE {

VEDecoderBase::VEDecoderBase(const std::shared_ptr<AMessage> &notify)
    : mNotify(notify),
      mSource(nullptr),
      mRenderer(nullptr),
      mPaused(false),
      mStarted(false),
      mFlushing(false),
      mShuttingDown(false) {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
}

VEDecoderBase::~VEDecoderBase() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
}

// --- Public interface (NuPlayer::DecoderBase style) ---

void VEDecoderBase::configure(const std::shared_ptr<VEDemux> &source) {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatConfigure, shared_from_this());
    msg->setObject("source", source);
    msg->post();
}

void VEDecoderBase::setRenderer(const std::shared_ptr<AHandler> &renderer) {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatSetRenderer, shared_from_this());
    msg->setObject("renderer", renderer);
    msg->post();
}

void VEDecoderBase::init() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
    msg->post();
}

void VEDecoderBase::start() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
    msg->post();
}

void VEDecoderBase::pause() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
    msg->post();
}

void VEDecoderBase::resume() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatResume, shared_from_this());
    msg->post();
}

void VEDecoderBase::flush() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
    msg->post();
}

void VEDecoderBase::shutdown() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatShutdown, shared_from_this());
    msg->post();
}

// --- Message handler ---

void VEDecoderBase::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatConfigure: {
            std::shared_ptr<void> tmp;
            if (msg->findObject("source", &tmp)) {
                mSource = std::static_pointer_cast<VEDemux>(tmp);
                onConfigure(mSource);
            }
            break;
        }

        case kWhatSetRenderer: {
            std::shared_ptr<void> tmp;
            if (msg->findObject("renderer", &tmp)) {
                mRenderer = std::static_pointer_cast<AHandler>(tmp);
                onSetRenderer(mRenderer);
            }
            break;
        }

        case kWhatInit: {
            onInit();
            break;
        }

        case kWhatStart: {
            mStarted = true;
            mPaused = false;
            onStart();
            break;
        }

        case kWhatPause: {
            mPaused = true;
            onPause();
            break;
        }

        case kWhatResume: {
            mPaused = false;
            onResume();
            notifyResumeComplete();
            break;
        }

        case kWhatFlush: {
            mFlushing = true;
            onFlush();
            mFlushing = false;
            notifyFlushComplete();
            break;
        }

        case kWhatShutdown: {
            mShuttingDown = true;
            onShutdown();
            notifyShutdownComplete();
            break;
        }

        case kWhatDecode: {
            if (mStarted && !mPaused && !mFlushing && !mShuttingDown) {
                onDecode();
            }
            break;
        }

        default:
            ALOGW("VEDecoderBase::%s - Unhandled message: %d", __FUNCTION__, msg->what());
            break;
    }
}

// --- Notification helpers (NuPlayer style) ---

void VEDecoderBase::notifyFlushComplete() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushCompleted);
    notify->post();
}

void VEDecoderBase::notifyShutdownComplete() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatShutdownCompleted);
    notify->post();
}

void VEDecoderBase::notifyResumeComplete() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatResumeCompleted);
    notify->post();
}

void VEDecoderBase::notifyEOS() {
    ALOGI("VEDecoderBase::%s", __FUNCTION__);
    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatEOS);
    notify->post();
}

void VEDecoderBase::notifyError(int err) {
    ALOGE("VEDecoderBase::%s err=%d", __FUNCTION__, err);
    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatError);
    notify->setInt32("err", err);
    notify->post();
}

} // namespace VE
