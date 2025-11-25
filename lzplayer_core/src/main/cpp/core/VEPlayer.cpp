#include "VEPlayer.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"
#include "platform/VEPlatform.h"
#include "VEDef.h"

#include <utility>

namespace VE {

VEPlayer::VEPlayer() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
}

VEPlayer::~VEPlayer() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
}

void VEPlayer::setListener(const std::shared_ptr<Listener> &listener) {
    std::lock_guard<std::mutex> lock(mLock);
    mListener = listener;
}

// NuPlayer::setDataSourceAsync style
VEResult VEPlayer::setDataSourceAsync(const std::string &url) {
    ALOGI("VEPlayer::%s url=%s", __FUNCTION__, url.c_str());
    
    auto msg = std::make_shared<AMessage>(kWhatSetDataSource, shared_from_this());
    msg->setString("url", url);
    msg->post();
    
    return VE_OK;
}

// NuPlayer::setVideoSurfaceTextureAsync style
VEResult VEPlayer::setVideoSurfaceTextureAsync(VENativeWindow surfaceTexture, int width, int height) {
    ALOGI("VEPlayer::%s surfaceTexture=%p width=%d height=%d", 
          __FUNCTION__, surfaceTexture, width, height);
    
    auto msg = std::make_shared<AMessage>(kWhatSetVideoSurface, shared_from_this());
    msg->setPointer("surface", static_cast<void*>(surfaceTexture));
    msg->setInt32("width", width);
    msg->setInt32("height", height);
    msg->post();
    
    return VE_OK;
}

// NuPlayer::prepareAsync style
VEResult VEPlayer::prepareAsync() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    auto msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
    msg->post();
    
    return VE_OK;
}

// NuPlayer::start style
VEResult VEPlayer::start() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    auto msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
    msg->post();
    
    return VE_OK;
}

// NuPlayer::pause style
VEResult VEPlayer::pause() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    auto msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
    msg->post();
    
    return VE_OK;
}

// NuPlayer::resume style
VEResult VEPlayer::resume() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    auto msg = std::make_shared<AMessage>(kWhatResume, shared_from_this());
    msg->post();
    
    return VE_OK;
}

// NuPlayer stop (no direct equivalent, we implement it)
VEResult VEPlayer::stop() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    auto msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
    msg->post();
    
    return VE_OK;
}

// NuPlayer::resetAsync style
VEResult VEPlayer::resetAsync() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    auto msg = std::make_shared<AMessage>(kWhatReset, shared_from_this());
    msg->post();
    
    return VE_OK;
}

// NuPlayer::seekToAsync style
VEResult VEPlayer::seekToAsync(int64_t seekTimeUs, int mode) {
    ALOGI("VEPlayer::%s seekTimeUs=%" PRId64 " mode=%d", __FUNCTION__, seekTimeUs, mode);
    
    auto msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
    msg->setInt64("seekTimeUs", seekTimeUs);
    msg->setInt32("mode", mode);
    msg->post();
    
    return VE_OK;
}

int64_t VEPlayer::getCurrentPosition() {
    std::lock_guard<std::mutex> lock(mLock);
    // TODO: Get current position from renderer/sync
    return 0;
}

int64_t VEPlayer::getDuration() {
    std::lock_guard<std::mutex> lock(mLock);
    if (mMediaInfo != nullptr) {
        return mMediaInfo->duration;
    }
    return 0;
}

VEResult VEPlayer::setPlaybackSettings(float rate) {
    ALOGI("VEPlayer::%s rate=%f", __FUNCTION__, rate);
    std::lock_guard<std::mutex> lock(mLock);
    mPlaybackRate = rate;
    // TODO: Apply playback speed to renderer
    return VE_OK;
}

VEResult VEPlayer::getPlaybackSettings(float *rate) {
    std::lock_guard<std::mutex> lock(mLock);
    if (rate == nullptr) {
        return VE_BAD_VALUE;
    }
    *rate = mPlaybackRate;
    return VE_OK;
}

VEResult VEPlayer::setLooping(bool looping) {
    ALOGI("VEPlayer::%s looping=%d", __FUNCTION__, looping);
    std::lock_guard<std::mutex> lock(mLock);
    mLooping = looping;
    return VE_OK;
}

bool VEPlayer::isLooping() {
    std::lock_guard<std::mutex> lock(mLock);
    return mLooping;
}

// ============= Message Handler (NuPlayer::onMessageReceived style) =============
void VEPlayer::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatSetDataSource:
            onSetDataSource(msg);
            break;

        case kWhatPrepare:
            onPrepareAsync();
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

        case kWhatStop:
            onStop();
            break;

        case kWhatReset:
            onReset();
            break;

        case kWhatSeek:
            onSeek(msg);
            break;

        case kWhatSourceNotify:
            onSourceNotify(msg);
            break;

        case kWhatVideoNotify:
            onVideoNotify(msg);
            break;

        case kWhatAudioNotify:
            onAudioNotify(msg);
            break;

        case kWhatRendererNotify:
            onRendererNotify(msg);
            break;

        default:
            ALOGW("VEPlayer::%s unhandled message: %d", __FUNCTION__, msg->what());
            break;
    }
}

// ============= Internal Handlers (NuPlayer style) =============

void VEPlayer::onSetDataSource(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    std::string url;
    if (!msg->findString("url", url) || url.empty()) {
        ALOGE("VEPlayer::%s - Invalid url", __FUNCTION__);
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, VE_PLAYER_ERROR_UNKNOWN, 0);
        return;
    }
    
    mDataSourcePath = url;
    
    // Create notification message for source
    mNotifyMsg = std::make_shared<AMessage>(kWhatSourceNotify, shared_from_this());
    
    // Create source (Demux) - similar to GenericSource in NuPlayer
    mSourceLooper = std::make_shared<ALooper>();
    mSourceLooper->setName("source_looper");
    mSourceLooper->start(false);
    
    mSource = std::make_shared<VEDemux>(mNotifyMsg);
    mSourceLooper->registerHandler(mSource);
    
    ALOGI("VEPlayer::%s - Source created for url: %s", __FUNCTION__, url.c_str());
}

void VEPlayer::onPrepareAsync() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    if (mSource == nullptr) {
        ALOGE("VEPlayer::%s - Source not set", __FUNCTION__);
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, VE_PLAYER_ERROR_UNKNOWN, 0);
        return;
    }
    
    // Prepare source (open file, parse tracks)
    VEBundle params;
    params.set("path", mDataSourcePath);
    
    if (mSource->prepare(params) != VE_OK) {
        ALOGE("VEPlayer::%s - Failed to prepare source", __FUNCTION__);
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, VE_PLAYER_ERROR_OPEN_DEMUX_FAILED, 0);
        return;
    }
    
    mMediaInfo = mSource->getFileInfo();
    if (mMediaInfo == nullptr) {
        ALOGE("VEPlayer::%s - Failed to get media info", __FUNCTION__);
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, VE_PLAYER_ERROR_UNKNOWN, 0);
        return;
    }
    
    // Create AV sync controller
    mAVSync = std::make_shared<VEAVsync>();
    
    // Instantiate decoders based on available tracks (NuPlayer::instantiateDecoder style)
    if (mMediaInfo->audio_stream_index != -1) {
        // Create audio decoder
        mAudioDecoderLooper = std::make_shared<ALooper>();
        mAudioDecoderLooper->setName("audio_decoder");
        mAudioDecoderLooper->start(false);
        
        auto audioNotify = std::make_shared<AMessage>(kWhatAudioNotify, shared_from_this());
        mAudioDecoder = std::make_shared<VEAudioDecoder>(audioNotify);
        mAudioDecoderLooper->registerHandler(mAudioDecoder);
        mAudioDecoder->prepare(mSource);
        
        // Create audio renderer
        mAudioRendererLooper = std::make_shared<ALooper>();
        mAudioRendererLooper->setName("audio_renderer");
        mAudioRendererLooper->start(false);
        
        auto audioRenderNotify = std::make_shared<AMessage>(kWhatRendererNotify, shared_from_this());
        mAudioRenderer = std::make_shared<VEAudioRender>(audioRenderNotify, mAVSync);
        mAudioRendererLooper->registerHandler(mAudioRenderer);
        
        VEBundle audioParams;
        audioParams.set("samplerate", 44100);
        audioParams.set("channel", 2);
        audioParams.set("format", 1);
        audioParams.set("decode", mAudioDecoder);
        mAudioRenderer->prepare(audioParams);
        
        ALOGI("VEPlayer::%s - Audio decoder and renderer created", __FUNCTION__);
    }
    
    if (mMediaInfo->video_stream_index != -1) {
        // Create video decoder
        mVideoDecoderLooper = std::make_shared<ALooper>();
        mVideoDecoderLooper->setName("video_decoder");
        mVideoDecoderLooper->start(false);
        
        auto videoNotify = std::make_shared<AMessage>(kWhatVideoNotify, shared_from_this());
        mVideoDecoder = std::make_shared<VEVideoDecoder>(videoNotify);
        mVideoDecoderLooper->registerHandler(mVideoDecoder);
        mVideoDecoder->prepare(mSource);
        
        // Create video renderer
        mVideoRendererLooper = std::make_shared<ALooper>();
        mVideoRendererLooper->setName("video_renderer");
        mVideoRendererLooper->start(false);
        
        auto videoRenderNotify = std::make_shared<AMessage>(kWhatRendererNotify, shared_from_this());
        mVideoRenderer = std::make_shared<VEVideoDisplay>(videoRenderNotify, mAVSync);
        mVideoRendererLooper->registerHandler(mVideoRenderer);
        
        VEBundle videoParams;
        videoParams.set("surface", mNativeWindow);
        videoParams.set("width", mSurfaceWidth);
        videoParams.set("height", mSurfaceHeight);
        videoParams.set("fps", mMediaInfo->fps);
        videoParams.set("decoder", mVideoDecoder);
        mVideoRenderer->prepare(videoParams);
        
        ALOGI("VEPlayer::%s - Video decoder and renderer created", __FUNCTION__);
    }
    
    // Notify prepared
    notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PREPARED, 0, 0);
    
    ALOGI("VEPlayer::%s - Prepare complete", __FUNCTION__);
}

void VEPlayer::onStart() {
    ALOGI("VEPlayer::%s mStarted=%d mPaused=%d", __FUNCTION__, mStarted, mPaused);
    
    if (mStarted && mPaused) {
        // Already started, just resume
        onResume();
        return;
    }
    
    if (mStarted) {
        // Already started and running
        return;
    }
    
    mStarted = true;
    mPaused = false;
    mPausedByClient = false;
    
    // Start components in order: renderers -> decoders -> source (NuPlayer style)
    if (mVideoRenderer) {
        mVideoRenderer->start();
    }
    if (mAudioRenderer) {
        mAudioRenderer->start();
    }
    if (mVideoDecoder) {
        mVideoDecoder->start();
    }
    if (mAudioDecoder) {
        mAudioDecoder->start();
    }
    if (mSource && !mSourceStarted) {
        mSource->start();
        mSourceStarted = true;
    }
    
    ALOGI("VEPlayer::%s - Playback started", __FUNCTION__);
}

void VEPlayer::onPause() {
    ALOGI("VEPlayer::%s mStarted=%d mPaused=%d", __FUNCTION__, mStarted, mPaused);
    
    if (!mStarted || mPaused) {
        return;
    }
    
    mPaused = true;
    mPausedByClient = true;
    
    // Pause all components (NuPlayer style)
    if (mVideoRenderer) {
        mVideoRenderer->pause();
    }
    if (mAudioRenderer) {
        mAudioRenderer->pause();
    }
    if (mVideoDecoder) {
        mVideoDecoder->pause();
    }
    if (mAudioDecoder) {
        mAudioDecoder->pause();
    }
    if (mSource) {
        mSource->pause();
    }
    
    ALOGI("VEPlayer::%s - Playback paused", __FUNCTION__);
}

void VEPlayer::onResume() {
    ALOGI("VEPlayer::%s mPaused=%d", __FUNCTION__, mPaused);
    
    if (!mPaused) {
        return;
    }
    
    mPaused = false;
    mPausedByClient = false;
    
    // Resume all components (NuPlayer style)
    if (mVideoRenderer) {
        mVideoRenderer->start();
    }
    if (mAudioRenderer) {
        mAudioRenderer->start();
    }
    if (mVideoDecoder) {
        mVideoDecoder->start();
    }
    if (mAudioDecoder) {
        mAudioDecoder->start();
    }
    if (mSource) {
        mSource->start();
    }
    
    ALOGI("VEPlayer::%s - Playback resumed", __FUNCTION__);
}

void VEPlayer::onStop() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    mStarted = false;
    mPaused = false;
    mSourceStarted = false;
    
    // Stop all components
    if (mVideoRenderer) {
        mVideoRenderer->stop();
    }
    if (mAudioRenderer) {
        mAudioRenderer->stop();
    }
    if (mVideoDecoder) {
        mVideoDecoder->stop();
    }
    if (mAudioDecoder) {
        mAudioDecoder->stop();
    }
    if (mSource) {
        mSource->stop();
    }
    
    ALOGI("VEPlayer::%s - Playback stopped", __FUNCTION__);
}

void VEPlayer::onReset() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    // Stop everything first
    onStop();
    
    // Release all components (NuPlayer style shutdown)
    if (mVideoRenderer) {
        mVideoRenderer->release();
        mVideoRenderer = nullptr;
    }
    if (mAudioRenderer) {
        mAudioRenderer->release();
        mAudioRenderer = nullptr;
    }
    if (mVideoDecoder) {
        mVideoDecoder->release();
        mVideoDecoder = nullptr;
    }
    if (mAudioDecoder) {
        mAudioDecoder->release();
        mAudioDecoder = nullptr;
    }
    if (mSource) {
        mSource->release();
        mSource = nullptr;
    }
    
    // Stop loopers
    if (mVideoRendererLooper) {
        mVideoRendererLooper->stop();
        mVideoRendererLooper = nullptr;
    }
    if (mAudioRendererLooper) {
        mAudioRendererLooper->stop();
        mAudioRendererLooper = nullptr;
    }
    if (mVideoDecoderLooper) {
        mVideoDecoderLooper->stop();
        mVideoDecoderLooper = nullptr;
    }
    if (mAudioDecoderLooper) {
        mAudioDecoderLooper->stop();
        mAudioDecoderLooper = nullptr;
    }
    if (mSourceLooper) {
        mSourceLooper->stop();
        mSourceLooper = nullptr;
    }
    
    // Clear native window
    if (mNativeWindow) {
#if VE_PLATFORM_ANDROID
        ANativeWindow_release(mNativeWindow);
#endif
        mNativeWindow = nullptr;
    }
    
    // Reset state
    mMediaInfo = nullptr;
    mAVSync = nullptr;
    mAudioEOS = false;
    mVideoEOS = false;
    mSentEOS = false;
    mSeeking = false;
    mFlushingAudio = NONE;
    mFlushingVideo = NONE;
    mPlaybackRate = 1.0f;
    
    ALOGI("VEPlayer::%s - Reset complete", __FUNCTION__);
}

void VEPlayer::onSeek(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    int64_t seekTimeUs = 0;
    int mode = 0;
    
    if (!msg->findInt64("seekTimeUs", &seekTimeUs)) {
        ALOGE("VEPlayer::%s - No seek time specified", __FUNCTION__);
        return;
    }
    msg->findInt32("mode", &mode);
    
    performSeek(seekTimeUs, mode);
}

void VEPlayer::onSetVideoSurface(const std::shared_ptr<AMessage> &msg) {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    void* surface = nullptr;
    msg->findPointer("surface", &surface);
    msg->findInt32("width", &mSurfaceWidth);
    msg->findInt32("height", &mSurfaceHeight);
    
    mNativeWindow = static_cast<VENativeWindow>(surface);
    
    // If video renderer exists, update surface (NuPlayer style)
    if (mVideoRenderer) {
        mVideoRenderer->setSurface(mNativeWindow, mSurfaceWidth, mSurfaceHeight);
    }
    
    ALOGI("VEPlayer::%s - Surface set: %p %dx%d", __FUNCTION__, surface, mSurfaceWidth, mSurfaceHeight);
}

void VEPlayer::onSourceNotify(const std::shared_ptr<AMessage> &msg) {
    int32_t what = 0;
    msg->findInt32("what", &what);
    
    ALOGI("VEPlayer::%s what=%d", __FUNCTION__, what);
    
    // Handle source notifications (EOS, error, etc.)
    switch (what) {
        case VE_NOTIFY_EVENT_EOS:
            // Source has reached end of stream
            break;
            
        case VE_NOTIFY_EVENT_ERROR:
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, VE_PLAYER_ERROR_UNKNOWN, 0);
            break;
            
        default:
            break;
    }
}

void VEPlayer::onVideoNotify(const std::shared_ptr<AMessage> &msg) {
    int32_t what = 0;
    msg->findInt32("event", &what);
    
    ALOGI("VEPlayer::%s what=%d", __FUNCTION__, what);
    
    switch (what) {
        case VE_NOTIFY_EVENT_EOS:
            mVideoEOS = true;
            handleEOS();
            break;
            
        case VE_NOTIFY_EVENT_FLUSH_DONE:
            mFlushingVideo = FLUSHED;
            finishFlushIfPossible();
            break;
            
        default:
            break;
    }
}

void VEPlayer::onAudioNotify(const std::shared_ptr<AMessage> &msg) {
    int32_t what = 0;
    msg->findInt32("event", &what);
    
    ALOGI("VEPlayer::%s what=%d", __FUNCTION__, what);
    
    switch (what) {
        case VE_NOTIFY_EVENT_EOS:
            mAudioEOS = true;
            handleEOS();
            break;
            
        case VE_NOTIFY_EVENT_FLUSH_DONE:
            mFlushingAudio = FLUSHED;
            finishFlushIfPossible();
            break;
            
        default:
            break;
    }
}

void VEPlayer::onRendererNotify(const std::shared_ptr<AMessage> &msg) {
    int32_t what = 0;
    msg->findInt32("event", &what);
    
    ALOGI("VEPlayer::%s what=%d", __FUNCTION__, what);
    
    switch (what) {
        case VE_NOTIFY_EVENT_EOS:
            // Renderer has finished playing all buffered data
            {
                int32_t type = 0;
                msg->findInt32("type", &type);
                if (type == E_COMPONENT_TYPE_AUDIO_RENDER) {
                    mAudioEOS = true;
                } else if (type == E_COMPONENT_TYPE_VIDEO_RENDER) {
                    mVideoEOS = true;
                }
                handleEOS();
            }
            break;
            
        case VE_NOTIFY_EVENT_PROGRESS:
            {
                int64_t progress = 0;
                msg->findInt64("arg3", &progress);
                // Convert to milliseconds and notify
                notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS, 0, 
                               static_cast<int>((double)progress * 1000.0 / AV_TIME_BASE));
            }
            break;
            
        case VE_NOTIFY_EVENT_SEEK_DONE:
            finishSeek();
            break;
            
        default:
            break;
    }
}

// ============= Helper Methods (NuPlayer style) =============

void VEPlayer::performSeek(int64_t seekTimeUs, int mode) {
    ALOGI("VEPlayer::%s seekTimeUs=%" PRId64 " mode=%d", __FUNCTION__, seekTimeUs, mode);
    
    if (mSeeking) {
        ALOGI("VEPlayer::%s - Already seeking, updating target", __FUNCTION__);
        mSeekTimeUs = seekTimeUs;
        mSeekMode = mode;
        return;
    }
    
    mSeeking = true;
    mSeekTimeUs = seekTimeUs;
    mSeekMode = mode;
    mAudioEOS = false;
    mVideoEOS = false;
    
    // Pause during seek if playing
    bool wasPlaying = mStarted && !mPaused;
    if (wasPlaying) {
        onPause();
    }
    
    // Flush decoders and renderers (NuPlayer style)
    if (mVideoRenderer) {
        mVideoRenderer->flush();
    }
    if (mAudioRenderer) {
        mAudioRenderer->flush();
    }
    if (mVideoDecoder) {
        mVideoDecoder->flush();
    }
    if (mAudioDecoder) {
        mAudioDecoder->flush();
    }
    
    // Seek source
    if (mSource) {
        mSource->seekTo(static_cast<double>(seekTimeUs) / 1000.0); // Convert to ms
    }
    
    // Resume if was playing
    if (wasPlaying) {
        onResume();
    }
    
    finishSeek();
}

void VEPlayer::finishSeek() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    
    mSeeking = false;
    notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE, 0, 0);
}

void VEPlayer::handleEOS() {
    ALOGI("VEPlayer::%s audioEOS=%d videoEOS=%d sentEOS=%d", 
          __FUNCTION__, mAudioEOS, mVideoEOS, mSentEOS);
    
    // Check if both audio and video have reached EOS (NuPlayer style)
    bool audioComplete = (mAudioDecoder == nullptr) || mAudioEOS;
    bool videoComplete = (mVideoDecoder == nullptr) || mVideoEOS;
    
    if (audioComplete && videoComplete && !mSentEOS) {
        mSentEOS = true;
        
        if (mLooping) {
            // Seek to beginning for looping (NuPlayer style)
            performSeek(0, 0);
            mSentEOS = false;
        } else {
            // Notify completion
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION, 0, 0);
        }
    }
}

void VEPlayer::finishFlushIfPossible() {
    ALOGI("VEPlayer::%s audio=%d video=%d", __FUNCTION__, mFlushingAudio, mFlushingVideo);
    
    if (mFlushingAudio != NONE && mFlushingAudio != FLUSHED) {
        return;
    }
    if (mFlushingVideo != NONE && mFlushingVideo != FLUSHED) {
        return;
    }
    
    // Both flushed, reset state
    mFlushingAudio = NONE;
    mFlushingVideo = NONE;
}

void VEPlayer::finishPrepare() {
    ALOGI("VEPlayer::%s", __FUNCTION__);
    notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PREPARED, 0, 0);
}

void VEPlayer::notifyListener(int msg, int ext1, int ext2, const void *obj) {
    auto listener = mListener.lock();
    if (listener) {
        listener->notify(msg, ext1, ext2, obj);
    }
}

} // namespace VE