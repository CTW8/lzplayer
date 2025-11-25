//
// VERenderer.cpp
// NuPlayer-style unified Renderer implementation
// Reference: frameworks/av/media/libmediaplayerservice/nuplayer/NuPlayerRenderer.cpp
//

#include "VERenderer.h"
#include "Log.h"
#include "VEDef.h"
#include "renders/VEAudioSLESRender.h"
#include "renders/VEGLESVideoRenderer.h"

#include <chrono>

namespace VE {

VERenderer::VERenderer(const std::shared_ptr<AMessage> &notify,
                       const std::shared_ptr<VEAVsync> &avSync)
    : mNotify(notify),
      mAVSync(avSync) {
    ALOGI("VERenderer::%s", __FUNCTION__);
}

VERenderer::~VERenderer() {
    ALOGI("VERenderer::%s", __FUNCTION__);
}

// --- Public interface (NuPlayer::Renderer style) ---

void VERenderer::setVideoSurface(VENativeWindow nativeWindow, int width, int height) {
    ALOGI("VERenderer::%s nativeWindow=%p width=%d height=%d",
          __FUNCTION__, nativeWindow, width, height);

    auto msg = std::make_shared<AMessage>(kWhatSetVideoSurface, shared_from_this());
    msg->setPointer("nativeWindow", static_cast<void*>(nativeWindow));
    msg->setInt32("width", width);
    msg->setInt32("height", height);
    msg->post();
}

void VERenderer::openAudioSink(int sampleRate, int channelCount, int format) {
    ALOGI("VERenderer::%s sampleRate=%d channelCount=%d format=%d",
          __FUNCTION__, sampleRate, channelCount, format);

    auto msg = std::make_shared<AMessage>(kWhatOpenAudioSink, shared_from_this());
    msg->setInt32("sampleRate", sampleRate);
    msg->setInt32("channelCount", channelCount);
    msg->setInt32("format", format);
    msg->post();
}

void VERenderer::closeAudioSink() {
    ALOGI("VERenderer::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatCloseAudioSink, shared_from_this());
    msg->post();
}

void VERenderer::queueBuffer(bool audio, const std::shared_ptr<VEFrame> &buffer) {
    ALOGD("VERenderer::%s audio=%d", __FUNCTION__, audio);

    auto msg = std::make_shared<AMessage>(kWhatQueueBuffer, shared_from_this());
    msg->setInt32("audio", audio ? 1 : 0);
    msg->setObject("buffer", buffer);
    msg->post();
}

void VERenderer::queueEOS(bool audio) {
    ALOGI("VERenderer::%s audio=%d", __FUNCTION__, audio);

    auto msg = std::make_shared<AMessage>(kWhatQueueEOS, shared_from_this());
    msg->setInt32("audio", audio ? 1 : 0);
    msg->post();
}

void VERenderer::start() {
    ALOGI("VERenderer::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
    msg->post();
}

void VERenderer::pause() {
    ALOGI("VERenderer::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
    msg->post();
}

void VERenderer::resume() {
    ALOGI("VERenderer::%s", __FUNCTION__);
    auto msg = std::make_shared<AMessage>(kWhatResume, shared_from_this());
    msg->post();
}

void VERenderer::flush(bool audio, bool video) {
    ALOGI("VERenderer::%s audio=%d video=%d", __FUNCTION__, audio, video);

    auto msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
    msg->setInt32("audio", audio ? 1 : 0);
    msg->setInt32("video", video ? 1 : 0);
    msg->post();
}

int64_t VERenderer::getMediaTimeUs() {
    // Get current playback position based on audio anchor
    if (mAnchorTimeMediaUs < 0) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    int64_t realUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    int64_t mediaUs = mAnchorTimeMediaUs +
        static_cast<int64_t>((realUs - mAnchorTimeRealUs) * mPlaybackRate);

    return mediaUs;
}

void VERenderer::setPlaybackRate(float rate) {
    ALOGI("VERenderer::%s rate=%f", __FUNCTION__, rate);

    auto msg = std::make_shared<AMessage>(kWhatSetRate, shared_from_this());
    msg->setFloat("rate", rate);
    msg->post();
}

// --- Message handler ---

void VERenderer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatOpenAudioSink:
            onOpenAudioSink(msg);
            break;

        case kWhatCloseAudioSink:
            onCloseAudioSink();
            break;

        case kWhatSetVideoSurface:
            onSetVideoSurface(msg);
            break;

        case kWhatStart:
            onStart();
            break;

        case kWhatPause:
            onPause();
            break;

        case kWhatResume:
            onResume();
            break;

        case kWhatFlush:
            onFlush(msg);
            break;

        case kWhatQueueBuffer:
            onQueueBuffer(msg);
            break;

        case kWhatQueueEOS:
            onQueueEOS(msg);
            break;

        case kWhatDrainAudioQueue:
            onDrainAudioQueue();
            break;

        case kWhatDrainVideoQueue:
        case kWhatPostDrainVideoQueue:
            onDrainVideoQueue();
            break;

        case kWhatSetRate:
            onSetRate(msg);
            break;

        default:
            ALOGW("VERenderer::%s - Unhandled message: %d", __FUNCTION__, msg->what());
            break;
    }
}

// --- Internal handlers ---

void VERenderer::onOpenAudioSink(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VERenderer::%s", __FUNCTION__);

    msg->findInt32("sampleRate", &mSampleRate);
    msg->findInt32("channelCount", &mChannelCount);
    msg->findInt32("format", &mAudioFormat);

#if VE_PLATFORM_ANDROID
    // Create OpenSL ES audio renderer
    mAudioSink = std::make_shared<VEAudioSLESRender>();

    AudioConfig config;
    config.sampleRate = mSampleRate;
    config.channels = mChannelCount;
    config.sampleFormat = mAudioFormat;

    // Set up callback for audio drain
    auto selfShared = std::dynamic_pointer_cast<VERenderer>(shared_from_this());
    auto wSelf = std::weak_ptr<VERenderer>(selfShared);
    config.onCallback = [wSelf]() -> int {
        if (auto self = wSelf.lock()) {
            auto msg = std::make_shared<AMessage>(VERenderer::kWhatDrainAudioQueue, self);
            msg->post();
        }
        return 0;
    };

    mAudioSink->configure(config);
#endif

    ALOGI("VERenderer::%s - Audio sink opened: sr=%d ch=%d fmt=%d",
          __FUNCTION__, mSampleRate, mChannelCount, mAudioFormat);
}

void VERenderer::onCloseAudioSink() {
    ALOGI("VERenderer::%s", __FUNCTION__);

    if (mAudioSink) {
        mAudioSink->release();
        mAudioSink = nullptr;
    }
}

void VERenderer::onSetVideoSurface(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VERenderer::%s", __FUNCTION__);

    void* nativeWindow = nullptr;
    msg->findPointer("nativeWindow", &nativeWindow);
    msg->findInt32("width", &mVideoWidth);
    msg->findInt32("height", &mVideoHeight);

    mNativeWindow = static_cast<VENativeWindow>(nativeWindow);

#if VE_PLATFORM_ANDROID
    // Create OpenGL ES video renderer
    mVideoSink = std::make_shared<VEGLESVideoRenderer>();

    VEBundle params;
    params.set("surface", mNativeWindow);
    params.set("width", mVideoWidth);
    params.set("height", mVideoHeight);

    mVideoSink->initialize(params);
#endif

    ALOGI("VERenderer::%s - Video surface set: %dx%d",
          __FUNCTION__, mVideoWidth, mVideoHeight);
}

void VERenderer::onStart() {
    ALOGI("VERenderer::%s", __FUNCTION__);

    mStarted = true;
    mPaused = false;

    if (mAudioSink) {
        mAudioSink->start();
    }

    // Start draining queues
    postDrainAudioQueue();
    postDrainVideoQueue();
}

void VERenderer::onPause() {
    ALOGI("VERenderer::%s", __FUNCTION__);

    mPaused = true;

    if (mAudioSink) {
        mAudioSink->pause();
    }
}

void VERenderer::onResume() {
    ALOGI("VERenderer::%s", __FUNCTION__);

    mPaused = false;

    if (mAudioSink) {
        mAudioSink->start();
    }

    // Resume draining queues
    postDrainAudioQueue();
    postDrainVideoQueue();
}

void VERenderer::onFlush(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VERenderer::%s", __FUNCTION__);

    int32_t audio = 0, video = 0;
    msg->findInt32("audio", &audio);
    msg->findInt32("video", &video);

    std::lock_guard<std::mutex> lock(mQueueLock);

    if (audio) {
        mAudioQueue.clear();
        mAudioEOS = false;
        mAudioEOSGenerated = false;
        mDrainingAudio = false;

        if (mAudioSink) {
            mAudioSink->flush();
        }

        notifyFlushComplete(true);
    }

    if (video) {
        mVideoQueue.clear();
        mVideoEOS = false;
        mVideoEOSGenerated = false;
        mVideoRenderingStarted = false;
        mDrainingVideo = false;

        notifyFlushComplete(false);
    }

    // Reset anchor times
    mAnchorTimeMediaUs = -1;
    mAnchorTimeRealUs = -1;
}

void VERenderer::onQueueBuffer(const std::shared_ptr<AMessage> &msg) {
    int32_t audio = 0;
    msg->findInt32("audio", &audio);

    std::shared_ptr<void> tmp;
    msg->findObject("buffer", &tmp);
    auto buffer = std::static_pointer_cast<VEFrame>(tmp);

    if (buffer == nullptr) {
        ALOGW("VERenderer::%s - null buffer", __FUNCTION__);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mQueueLock);
        if (audio) {
            mAudioQueue.push_back(buffer);
            ALOGD("VERenderer::%s - queued audio buffer, size=%zu", __FUNCTION__, mAudioQueue.size());
        } else {
            mVideoQueue.push_back(buffer);
            ALOGD("VERenderer::%s - queued video buffer, size=%zu", __FUNCTION__, mVideoQueue.size());
        }
    }

    // Trigger draining
    if (audio && !mDrainingAudio && mStarted && !mPaused) {
        postDrainAudioQueue();
    }
    if (!audio && !mDrainingVideo && mStarted && !mPaused) {
        postDrainVideoQueue();
    }
}

void VERenderer::onQueueEOS(const std::shared_ptr<AMessage> &msg) {
    int32_t audio = 0;
    msg->findInt32("audio", &audio);

    ALOGI("VERenderer::%s audio=%d", __FUNCTION__, audio);

    if (audio) {
        mAudioEOS = true;
    } else {
        mVideoEOS = true;
    }
}

void VERenderer::onDrainAudioQueue() {
    if (mPaused || !mStarted) {
        mDrainingAudio = false;
        return;
    }

    std::shared_ptr<VEFrame> frame;
    {
        std::lock_guard<std::mutex> lock(mQueueLock);
        if (mAudioQueue.empty()) {
            mDrainingAudio = false;
            if (mAudioEOS && !mAudioEOSGenerated) {
                mAudioEOSGenerated = true;
                notifyEOS(true);
            }
            return;
        }
        frame = mAudioQueue.front();
        mAudioQueue.pop_front();
    }

    mDrainingAudio = true;

    if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
        ALOGI("VERenderer::%s - Audio EOS frame", __FUNCTION__);
        mAudioEOSGenerated = true;
        notifyEOS(true);
        mDrainingAudio = false;
        return;
    }

    // Render audio frame
    if (renderAudioFrame(frame)) {
        // Update anchor time for A/V sync
        if (mAnchorTimeMediaUs < 0) {
            mAnchorTimeMediaUs = frame->getPts();
            auto now = std::chrono::steady_clock::now();
            mAnchorTimeRealUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
        }

        // Update A/V sync
        if (mAVSync) {
            mAVSync->updateAudioPts(frame->getPts());
        }
    }

    // Audio draining is callback-driven via mAudioSink callback
    mDrainingAudio = false;
}

void VERenderer::onDrainVideoQueue() {
    if (mPaused || !mStarted) {
        mDrainingVideo = false;
        return;
    }

    std::shared_ptr<VEFrame> frame;
    {
        std::lock_guard<std::mutex> lock(mQueueLock);
        if (mVideoQueue.empty()) {
            mDrainingVideo = false;
            if (mVideoEOS && !mVideoEOSGenerated) {
                mVideoEOSGenerated = true;
                notifyEOS(false);
            }
            return;
        }
        frame = mVideoQueue.front();
        mVideoQueue.pop_front();
    }

    mDrainingVideo = true;

    if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
        ALOGI("VERenderer::%s - Video EOS frame", __FUNCTION__);
        mVideoEOSGenerated = true;
        notifyEOS(false);
        mDrainingVideo = false;
        return;
    }

    // Update A/V sync
    if (mAVSync) {
        mAVSync->updateVideoPts(frame->getPts());
    }

    // Check if we should drop frame (late)
    bool shouldDrop = mAVSync ? mAVSync->shouldDropFrame() : false;

    if (!shouldDrop) {
        // Render video frame
        if (renderVideoFrame(frame)) {
            if (!mVideoRenderingStarted) {
                mVideoRenderingStarted = true;
                notifyVideoRenderingStart();
            }

            // Notify position
            notifyPosition(frame->getPts());
        }
    } else {
        ALOGD("VERenderer::%s - Dropping video frame due to sync", __FUNCTION__);
    }

    // Schedule next video frame with proper delay
    int64_t delayUs = 0;
    if (mAVSync) {
        delayUs = mAVSync->getWaitTime();
    } else {
        delayUs = 33000; // Default 30fps
    }

    auto msg = std::make_shared<AMessage>(kWhatPostDrainVideoQueue, shared_from_this());
    msg->post(delayUs > 0 ? delayUs : 0);
}

void VERenderer::onSetRate(const std::shared_ptr<AMessage> &msg) {
    float rate = 1.0f;
    msg->findFloat("rate", &rate);

    ALOGI("VERenderer::%s rate=%f", __FUNCTION__, rate);
    mPlaybackRate = rate;
}

// --- Helpers ---

void VERenderer::postDrainAudioQueue() {
    if (mDrainingAudio) {
        return;
    }
    auto msg = std::make_shared<AMessage>(kWhatDrainAudioQueue, shared_from_this());
    msg->post();
}

void VERenderer::postDrainVideoQueue() {
    if (mDrainingVideo) {
        return;
    }
    auto msg = std::make_shared<AMessage>(kWhatDrainVideoQueue, shared_from_this());
    msg->post();
}

bool VERenderer::renderAudioFrame(const std::shared_ptr<VEFrame> &frame) {
    if (!mAudioSink || !frame) {
        return false;
    }
    VEResult result = mAudioSink->renderFrame(frame);
    return result == VE_OK;
}

bool VERenderer::renderVideoFrame(const std::shared_ptr<VEFrame> &frame) {
    if (!mVideoSink || !frame) {
        return false;
    }
    mVideoSink->renderFrame(frame);
    return true;
}

void VERenderer::notifyEOS(bool audio) {
    ALOGI("VERenderer::%s audio=%d", __FUNCTION__, audio);

    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatEOS);
    notify->setInt32("audio", audio ? 1 : 0);
    notify->post();
}

void VERenderer::notifyFlushComplete(bool audio) {
    ALOGI("VERenderer::%s audio=%d", __FUNCTION__, audio);

    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushComplete);
    notify->setInt32("audio", audio ? 1 : 0);
    notify->post();
}

void VERenderer::notifyPosition(int64_t positionUs) {
    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatPosition);
    notify->setInt64("positionUs", positionUs);
    notify->post();
}

void VERenderer::notifyVideoRenderingStart() {
    ALOGI("VERenderer::%s", __FUNCTION__);

    auto notify = mNotify->dup();
    notify->setInt32("what", kWhatVideoRenderingStart);
    notify->post();
}

} // namespace VE
