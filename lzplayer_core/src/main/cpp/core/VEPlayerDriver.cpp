//
// Created by 李振 on 2024/7/25.
//
// VEPlayerDriver.cpp - NuPlayerDriver-style implementation
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
      mSeeking(false),
      mSeekInProgress(-1),
      mPlaybackRate(1.0f) {
    
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    // Create player
    mPlayer = std::make_shared<VEPlayer>();
    
    // Create and start looper for player
    mLooper = std::make_shared<ALooper>();
    mLooper->setName("VEPlayerDriver");
    mLooper->start(false);
    mLooper->registerHandler(mPlayer);
    
    // Set this as the listener for player events
    // Note: We use a weak reference pattern through shared_from_this()
}

VEPlayerDriver::~VEPlayerDriver() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    // Stop the looper
    if (mLooper != nullptr) {
        mLooper->stop();
        mLooper = nullptr;
    }
    
    mPlayer = nullptr;
}

VEResult VEPlayerDriver::setListener(const std::shared_ptr<MediaPlayerListener> &listener) {
    std::lock_guard<std::mutex> lock(mLock);
    mListener = listener;
    return VE_OK;
}

VEResult VEPlayerDriver::setDataSource(const std::string &path) {
    ALOGI("VEPlayerDriver::%s path=%s", __FUNCTION__, path.c_str());
    
    std::lock_guard<std::mutex> lock(mLock);
    
    if (mState != STATE_IDLE) {
        ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
        return VE_INVALID_STATE;
    }
    
    mState = STATE_SET_DATASOURCE_PENDING;
    
    VEResult result = mPlayer->setDataSource(path);
    if (result == VE_OK) {
        mState = STATE_UNPREPARED;
    } else {
        mState = STATE_IDLE;
    }
    
    return result;
}

VEResult VEPlayerDriver::setVideoSurfaceTexture(VENativeWindow win, int width, int height) {
    ALOGI("VEPlayerDriver::%s win=%p width=%d height=%d", __FUNCTION__, win, width, height);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    return mPlayer->setVideoSurfaceTexture(win, width, height);
}

VEResult VEPlayerDriver::prepare() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    std::unique_lock<std::mutex> lock(mLock);
    
    if (mState != STATE_UNPREPARED && mState != STATE_STOPPED) {
        ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
        return VE_INVALID_STATE;
    }
    
    mState = STATE_PREPARING;
    
    // Set listener before prepare
    mPlayer->setListener(std::dynamic_pointer_cast<VEPlayer::Listener>(shared_from_this()));
    
    VEResult result = mPlayer->prepare();
    if (result != VE_OK) {
        mState = STATE_UNPREPARED;
        return result;
    }
    
    // Wait for prepare to complete
    mCondition.wait(lock, [this] {
        return mState == STATE_PREPARED || mState == STATE_IDLE;
    });
    
    return (mState == STATE_PREPARED) ? VE_OK : VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::prepareAsync() {
    ALOGI("VEPlayerDriver::%s", __FUNCTION__);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    if (mState != STATE_UNPREPARED && mState != STATE_STOPPED) {
        ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
        return VE_INVALID_STATE;
    }
    
    mState = STATE_PREPARING;
    
    // Set listener before prepare
    mPlayer->setListener(std::dynamic_pointer_cast<VEPlayer::Listener>(shared_from_this()));
    
    return mPlayer->prepareAsync();
}

VEResult VEPlayerDriver::start() {
    ALOGI("VEPlayerDriver::%s state=%d", __FUNCTION__, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    switch (mState) {
        case STATE_PREPARED:
        case STATE_STOPPED_AND_PREPARED:
        case STATE_PAUSED:
            // Valid states for start
            break;
            
        case STATE_RUNNING:
            // Already running
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
    
    switch (mState) {
        case STATE_RUNNING:
            // Valid state for pause
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
    
    switch (mState) {
        case STATE_RUNNING:
        case STATE_PAUSED:
        case STATE_PREPARED:
        case STATE_STOPPED_AND_PREPARED:
            // Valid states for stop
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
    
    mState = STATE_RESET_IN_PROGRESS;
    
    VEResult result = mPlayer->resetAsync();
    
    mDurationUs = -1;
    mPositionUs = -1;
    mLooping = false;
    mPlaybackRate = 1.0f;
    mState = STATE_IDLE;
    mAtEOS = false;
    mSeeking = false;
    
    return result;
}

VEResult VEPlayerDriver::seekTo(int64_t msec, int mode) {
    ALOGI("VEPlayerDriver::%s msec=%" PRId64 " mode=%d state=%d", 
          __FUNCTION__, msec, mode, mState);
    
    std::lock_guard<std::mutex> lock(mLock);
    
    switch (mState) {
        case STATE_PREPARED:
        case STATE_STOPPED_AND_PREPARED:
        case STATE_RUNNING:
        case STATE_PAUSED:
            // Valid states for seek
            break;
            
        default:
            ALOGE("VEPlayerDriver::%s - Invalid state: %d", __FUNCTION__, mState);
            return VE_INVALID_STATE;
    }
    
    if (mSeeking) {
        // Already seeking, just update target
        mSeekInProgress = msec;
        return VE_OK;
    }
    
    mSeeking = true;
    mSeekInProgress = msec;
    
    // Convert milliseconds to microseconds for player
    int64_t seekTimeUs = msec * 1000LL;
    
    return mPlayer->seekTo(seekTimeUs, mode);
}

int64_t VEPlayerDriver::getCurrentPosition() {
    std::lock_guard<std::mutex> lock(mLock);
    
    if (mSeeking) {
        return mSeekInProgress;
    }
    
    if (mPositionUs < 0) {
        return 0;
    }
    
    // Convert microseconds to milliseconds
    return mPositionUs / 1000LL;
}

int64_t VEPlayerDriver::getDuration() {
    std::lock_guard<std::mutex> lock(mLock);
    
    if (mDurationUs < 0) {
        mDurationUs = mPlayer->getDuration();
    }
    
    return mDurationUs;
}

VEResult VEPlayerDriver::setLooping(bool looping) {
    ALOGI("VEPlayerDriver::%s looping=%d", __FUNCTION__, looping);
    
    std::lock_guard<std::mutex> lock(mLock);
    mLooping = looping;
    
    return mPlayer->setLooping(looping);
}

VEResult VEPlayerDriver::setPlaybackSettings(float rate) {
    ALOGI("VEPlayerDriver::%s rate=%f", __FUNCTION__, rate);
    
    std::lock_guard<std::mutex> lock(mLock);
    mPlaybackRate = rate;
    
    return mPlayer->setPlaybackSettings(rate);
}

VEPlayerDriver::State VEPlayerDriver::getState() const {
    std::lock_guard<std::mutex> lock(mLock);
    return mState;
}

// ============= VEPlayer::Listener Implementation =============

void VEPlayerDriver::notify(int msg, int ext1, int ext2, const void *obj) {
    ALOGI("VEPlayerDriver::%s msg=%d ext1=%d ext2=%d", __FUNCTION__, msg, ext1, ext2);
    
    std::unique_lock<std::mutex> lock(mLock);
    
    switch (msg) {
        case VE_PLAYER_NOTIFY_EVENT_ON_PREPARED:
            ALOGI("VEPlayerDriver::notify - PREPARED");
            mState = STATE_PREPARED;
            mCondition.notify_all();
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION:
            ALOGI("VEPlayerDriver::notify - COMPLETION");
            mAtEOS = true;
            if (mLooping) {
                // Seek to beginning for looping
                lock.unlock();
                seekTo(0, 0);
            } else {
                mState = STATE_PAUSED;
                notifyListener_l(msg, ext1, ext2, obj);
            }
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_ERROR:
            ALOGE("VEPlayerDriver::notify - ERROR ext1=%d", ext1);
            mState = STATE_IDLE;
            mCondition.notify_all();
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE:
            ALOGI("VEPlayerDriver::notify - SEEK_DONE");
            mSeeking = false;
            mSeekInProgress = -1;
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS:
            // Update position and notify
            mPositionUs = ext2 * 1000LL; // Convert ms to us
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        case VE_PLAYER_NOTIFY_EVENT_ON_INFO:
            notifyListener_l(msg, ext1, ext2, obj);
            break;
            
        default:
            ALOGW("VEPlayerDriver::notify - Unknown msg: %d", msg);
            notifyListener_l(msg, ext1, ext2, obj);
            break;
    }
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