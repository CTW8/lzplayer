//
// Created by 李振 on 2024/7/25.
//
// VEPlayerDriver.cpp - NuPlayerDriver-style implementation
// Reference: frameworks/av/media/libmediaplayerservice/nuplayer/NuPlayerDriver.cpp
//

#include "VEPlayerDriver.h"
#include "VEDef.h"

namespace VE {

VEPlayerDriver::VEPlayerDriver()
    : mState(STATE_IDLE),
      mAtEOS(false),
      mLooping(false),
      mAutoLoop(false),
      mDurationUs(-1),
      mPositionUs(-1),
      mStartupSeekTimeUs(-1),
      mSeeking(false),
      mSeekInProgress(-1),
      mPlaybackRate(1.0f),
      mNumFramesTotal(0),
      mNumFramesDropped(0) {
    
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    // Create player
    mPlayer = std::make_shared<VEPlayer>();
    
    // Create and start looper for player (similar to NuPlayerDriver)
    mLooper = std::make_shared<ALooper>();
    mLooper->setName("VEPlayerDriver");
    mLooper->start(false);
    mLooper->registerHandler(mPlayer);
    
    // Create internal listener to bridge events from VEPlayer to VEPlayerDriver
    mPlayerListener = std::make_shared<PlayerListener>(this);
}

VEPlayerDriver::~VEPlayerDriver() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    // Stop the looper
    if (mLooper != nullptr) {
        mLooper->stop();
        mLooper = nullptr;
    }
    
    mPlayer = nullptr;
    mPlayerListener = nullptr;
}

VEResult VEPlayerDriver::setListener(const std::shared_ptr<MediaPlayerListener> &listener) {
    std::lock_guard<std::mutex> lock(mLock);
    mListener = listener;
    return VE_OK;
}

VEResult VEPlayerDriver::setDataSource(const std::string &url) {
    ALOGI("VEPlayerDriver::%s url=%s", __FUNCTION__, url.c_str());
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: Only allow setDataSource in IDLE state
    if (mState != STATE_IDLE) {
        ALOGE("VEPlayerDriver::%s - Invalid state: %d (expected IDLE)", __FUNCTION__, mState);
        return VE_INVALID_STATE;
    }
    
    mState = STATE_SET_DATASOURCE_PENDING;
    
    // Set listener on player before any operations
    mPlayer->setListener(mPlayerListener);
    
    // Call player's setDataSourceAsync (NuPlayer style - all async)
    VEResult result = mPlayer->setDataSourceAsync(url);
    if (result == VE_OK) {
        // NuPlayerDriver transitions to UNPREPARED after setDataSource
        mState = STATE_UNPREPARED;
    } else {
        mState = STATE_IDLE;
    }
    
    return result;
}

VEResult VEPlayerDriver::setVideoSurfaceTexture(VENativeWindow surfaceTexture, int width, int height) {
    ALOGI("VEPlayerDriver::%s surfaceTexture=%p width=%d height=%d", 
          __FUNCTION__, surfaceTexture, width, height);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: setVideoSurfaceTexture can be called in most states
    switch (mState) {
        case STATE_SET_DATASOURCE_PENDING:
        case STATE_IDLE:
            // Not allowed in these states
            return VE_INVALID_STATE;
        default:
            break;
    }
    
    return mPlayer->setVideoSurfaceTextureAsync(surfaceTexture, width, height);
}

VEResult VEPlayerDriver::prepare() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    std::unique_lock<std::mutex> lock(mLock);
    
    // NuPlayerDriver: prepare only allowed in UNPREPARED or STOPPED states
    switch (mState) {
        case STATE_UNPREPARED:
            mState = STATE_PREPARING;
            break;
        case STATE_STOPPED:
            mState = STATE_STOPPED_AND_PREPARING;
            break;
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    VEResult result = mPlayer->prepareAsync();
    if (result != VE_OK) {
        mState = STATE_UNPREPARED;
        return result;
    }
    
    // Wait for prepare to complete (synchronous prepare)
    while (mState == STATE_PREPARING || mState == STATE_STOPPED_AND_PREPARING) {
        mCondition.wait(lock);
    }
    
    return (mState == STATE_PREPARED || mState == STATE_STOPPED_AND_PREPARED) 
           ? VE_OK : VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::prepareAsync() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: prepareAsync only allowed in UNPREPARED or STOPPED states
    switch (mState) {
        case STATE_UNPREPARED:
            mState = STATE_PREPARING;
            break;
        case STATE_STOPPED:
            mState = STATE_STOPPED_AND_PREPARING;
            break;
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    return mPlayer->prepareAsync();
}

VEResult VEPlayerDriver::start() {
    ALOGI("VEPlayerDriver::%s state=%d", __FUNCTION__, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: start allowed in PREPARED, STOPPED_AND_PREPARED, PAUSED states
    switch (mState) {
        case STATE_PREPARED:
        case STATE_STOPPED_AND_PREPARED:
        case STATE_PAUSED:
            // Valid states for start
            break;
            
        case STATE_RUNNING:
            // Already running, return OK
            return VE_OK;
            
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    mAtEOS = false;
    
    VEResult result = mPlayer->start();
    if (result == VE_OK) {
        mState = STATE_RUNNING;
    }
    
    return result;
}

VEResult VEPlayerDriver::pause() {
    ALOGI("VEPlayerDriver::%s state=%d", __FUNCTION__, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: pause only allowed when RUNNING
    switch (mState) {
        case STATE_RUNNING:
            break;
            
        case STATE_PAUSED:
            // Already paused
            return VE_OK;
            
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    VEResult result = mPlayer->pause();
    if (result == VE_OK) {
        mState = STATE_PAUSED;
    }
    
    return result;
}

VEResult VEPlayerDriver::stop() {
    ALOGI("VEPlayerDriver::%s state=%d", __FUNCTION__, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: stop allowed from RUNNING, PAUSED, PREPARED states
    switch (mState) {
        case STATE_RUNNING:
        case STATE_PAUSED:
        case STATE_PREPARED:
        case STATE_STOPPED_AND_PREPARED:
            break;
            
        case STATE_STOPPED:
            // Already stopped
            return VE_OK;
            
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    VEResult result = mPlayer->stop();
    if (result == VE_OK) {
        mState = STATE_STOPPED;
    }
    
    return result;
}

VEResult VEPlayerDriver::reset() {
    ALOGI("VEPlayerDriver::%s state=%d", __FUNCTION__, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: reset can be called from any state
    mState = STATE_RESET_IN_PROGRESS;
    
    VEResult result = mPlayer->resetAsync();
    
    // Reset all internal state
    mDurationUs = -1;
    mPositionUs = -1;
    mStartupSeekTimeUs = -1;
    mLooping = false;
    mPlaybackRate = 1.0f;
    mAtEOS = false;
    mSeeking = false;
    mSeekInProgress = -1;
    mNumFramesTotal = 0;
    mNumFramesDropped = 0;
    
    mState = STATE_IDLE;
    
    return result;
}

VEResult VEPlayerDriver::seekTo(int64_t msec, int mode) {
    ALOGI("VEPlayerDriver::%s msec=%" PRId64 " mode=%d state=%d", 
          __FUNCTION__, msec, mode, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    // NuPlayerDriver: seekTo allowed in PREPARED, STOPPED_AND_PREPARED, PAUSED, RUNNING states
    switch (mState) {
        case STATE_PREPARED:
        case STATE_STOPPED_AND_PREPARED:
        case STATE_RUNNING:
        case STATE_PAUSED:
            break;
            
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    // If already seeking, just update target position (coalesce seeks)
    if (mSeeking) {
        mSeekInProgress = msec;
        return VE_OK;
    }
    
    mSeeking = true;
    mSeekInProgress = msec;
    
    // Convert milliseconds to microseconds for player
    int64_t seekTimeUs = msec * 1000LL;
    
    return mPlayer->seekToAsync(seekTimeUs, mode);
}

VEResult VEPlayerDriver::getCurrentPosition(int64_t *msec) {
    std::lock_guard<std::mutex> lock(mLock);
    
    if (msec == nullptr) {
        return VE_BAD_VALUE;
    }
    
    // If seeking, return the seek target position
    if (mSeeking) {
        *msec = mSeekInProgress;
        return VE_OK;
    }
    
    // If position not available, return 0
    if (mPositionUs < 0) {
        *msec = 0;
        return VE_OK;
    }
    
    // Convert microseconds to milliseconds
    *msec = mPositionUs / 1000LL;
    return VE_OK;
}

VEResult VEPlayerDriver::getDuration(int64_t *msec) {
    std::lock_guard<std::mutex> lock(mLock);
    
    if (msec == nullptr) {
        return VE_BAD_VALUE;
    }
    
    // If duration not cached, get it from player
    if (mDurationUs < 0) {
        mDurationUs = mPlayer->getDuration();
    }
    
    *msec = mDurationUs / 1000LL;
    return VE_OK;
}

VEResult VEPlayerDriver::setLooping(bool loop) {
    ALOGI("VEPlayerDriver::%s loop=%d", __FUNCTION__, loop);
    
    std::lock_guard<std::mutex> lock(mLock);
    mLooping = loop;
    
    return mPlayer->setLooping(loop);
}

bool VEPlayerDriver::isLooping() {
    std::lock_guard<std::mutex> lock(mLock);
    return mLooping;
}

VEResult VEPlayerDriver::setPlaybackSettings(float rate) {
    ALOGI("VEPlayerDriver::%s rate=%f", __FUNCTION__, rate);
    
    std::lock_guard<std::mutex> lock(mLock);
    mPlaybackRate = rate;
    
    return mPlayer->setPlaybackSettings(rate);
}

VEResult VEPlayerDriver::getPlaybackSettings(float *rate) {
    std::lock_guard<std::mutex> lock(mLock);
    
    if (rate == nullptr) {
        return VE_BAD_VALUE;
    }
    
    *rate = mPlaybackRate;
    return VE_OK;
}

VEPlayerDriver::State VEPlayerDriver::getState() const {
    std::lock_guard<std::mutex> lock(mLock);
    return mState;
}

bool VEPlayerDriver::isPlaying() {
    std::lock_guard<std::mutex> lock(mLock);
    return mState == STATE_RUNNING;
}

// ============= Player Event Handler (similar to NuPlayerDriver::notifyListener) =============

void VEPlayerDriver::onPlayerNotify(int msg, int ext1, int ext2, const void *obj) {
    ALOGI("VEPlayerDriver::%s msg=%d ext1=%d ext2=%d", __FUNCTION__, msg, ext1, ext2);
    
    std::unique_lock<std::mutex> lock(mLock);
    
    switch (msg) {
        case VE_PLAYER_NOTIFY_EVENT_ON_PREPARED:
            ALOGI("VEPlayerDriver::onPlayerNotify - PREPARED");
            // Update state based on previous state
            if (mState == STATE_PREPARING) {
                mState = STATE_PREPARED;
            } else if (mState == STATE_STOPPED_AND_PREPARING) {
                mState = STATE_STOPPED_AND_PREPARED;
            }
            mCondition.notify_all();
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION:
            ALOGI("VEPlayerDriver::onPlayerNotify - COMPLETION");
            mAtEOS = true;
            if (mLooping) {
                // Seek to beginning for looping (NuPlayerDriver style)
                mSeeking = true;
                mSeekInProgress = 0;
                lock.unlock();
                mPlayer->seekToAsync(0, 0);
            } else {
                // State remains RUNNING but at EOS
                notifyListener_l(msg, ext1, ext2, obj);
            }
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_ERROR:
            ALOGE("VEPlayerDriver::onPlayerNotify - ERROR ext1=%d", ext1);
            // On error, reset state
            mState = STATE_IDLE;
            mCondition.notify_all();
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE:
            ALOGI("VEPlayerDriver::onPlayerNotify - SEEK_DONE");
            notifySeekComplete_l();
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS:
            // Update cached position (ext2 is in milliseconds)
            mPositionUs = ext2 * 1000LL;
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_INFO:
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        default:
            ALOGW("VEPlayerDriver::onPlayerNotify - Unknown msg: %d", msg);
            notifyListener_l(msg, ext1, ext2, obj);
            break;
    }
}

void VEPlayerDriver::notifySeekComplete_l() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    mSeeking = false;
    mSeekInProgress = -1;
    
    notifyListener_l(VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE, 0, 0, nullptr);
}

void VEPlayerDriver::notifyListener_l(int msg, int ext1, int ext2, const void *obj) {
    auto listener = mListener.lock();
    if (listener) {
        listener->notify(msg, ext1, ext2, obj);
    }
}

bool VEPlayerDriver::isValidStateForOperation_l(const char *operation) const {
    ALOGI("VEPlayerDriver::%s operation=%s state=%d", __FUNCTION__, operation, mState);
    
    switch (mState) {
        case STATE_IDLE:
        case STATE_RESET_IN_PROGRESS:
            return false;
        default:
            return true;
    }
}

} // namespace VE