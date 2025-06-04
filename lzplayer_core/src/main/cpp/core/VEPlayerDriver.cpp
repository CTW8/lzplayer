//
// Created by 李振 on 2024/7/25.
//

#include "VEPlayerDriver.h"
#include "VEDef.h"

VEPlayerDriver::VEPlayerDriver()
        : currentState(MEDIA_PLAYER_IDLE), mPlayer(std::make_shared<VEPlayer>()) {

    mPlayerLooper = std::make_shared<ALooper>();
    mPlayerLooper->setName("player_thread");
    mPlayerLooper->start(false);
    mPlayerLooper->registerHandler(mPlayer);

    mPlayer->setOnInfoListener([this](int code,double arg1,std::string str1,void *obj3) {
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_INFO, code, arg1, obj3);
    });

    mPlayer->setOnProgressListener([this](double progress) {
        ALOGD("setOnProgressListener progress:%f",progress);
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS, 0, progress, nullptr);
    });

    // 设置出错回调
    mPlayer->setOnErrorListener([this](int msg1,std::string msg2) {
        // 遇到错误直接切换到 MEDIA_PLAYER_STATE_ERROR
        currentState = MEDIA_PLAYER_STATE_ERROR;
        ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_ERROR enter!!!");
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, msg1, 0,&msg2);
        return true;
    });

    // 设置播放结束回调
    mPlayer->setOnCompletionListener([this]() {
            // 否则进入播放完成状态
            currentState = MEDIA_PLAYER_PLAYBACK_COMPLETE;
        ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION enter!!!");
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION, 0, 0, nullptr);
    });

    mPlayer->setOnPreparedListener([this](){
        currentState = MEDIA_PLAYER_PREPARED;
        mCond.notify_one();
        ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_PREPARED enter!!!");
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PREPARED,0,0, nullptr);
    });

    mPlayer->setOnSeekComplateListener([this](){
        std::lock_guard<std::mutex> lk(mMutex);
        mIsSeeking = false;
        ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE enter!!!");
        if (currentState == MEDIA_PLAYER_STARTED) {
            mPlayer->start();
        } else if (currentState == MEDIA_PLAYER_PAUSED) {
            mPlayer->pause();
        }
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE,0,0, nullptr);
    });

    mPlayer->setOnEOSListener([this](){
        std::lock_guard<std::mutex> lk(mMutex);
        ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_EOS enter!!!");
        mIsSeeking = false;
    });
}

VEPlayerDriver::~VEPlayerDriver() {}

VEResult VEPlayerDriver::setDataSource(std::string path) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (currentState != MEDIA_PLAYER_IDLE) {
        return VE_INVALID_OPERATION;
    }
    if (mPlayer->setDataSource(path) == 0) {
        currentState = MEDIA_PLAYER_INITIALIZED;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::setSurface(ANativeWindow *win, int width, int height) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mPlayer->setDisplayOut(win, width, height) == 0) {
        return VE_OK;
    }
    return VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::releaseSurface() {
    std::lock_guard<std::mutex> lk(mMutex);
    mPlayer->releaseSurface();
    return VE_OK;
}

VEResult VEPlayerDriver::prepare() {
    std::unique_lock<std::mutex> lk(mMutex);
    if (currentState != MEDIA_PLAYER_STOPPED && currentState != MEDIA_PLAYER_INITIALIZED) {
        ALOGD("VEPlayerDriver::%s player status not correct!!!",__FUNCTION__ );
        return VE_UNKNOWN_ERROR;
    }

    mPlayer->prepare();

    mCond.wait(lk);

    return VE_OK;
}

VEResult VEPlayerDriver::prepareAsync() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (currentState != MEDIA_PLAYER_STOPPED && currentState != MEDIA_PLAYER_INITIALIZED) {
        ALOGE("Invalid state for prepareAsync: %d", currentState);
        return VE_INVALID_OPERATION;
    }
    if (mPlayer->prepareAsync() == 0) {
        currentState = MEDIA_PLAYER_PREPARING;
        return VE_OK;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::start() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
        return VE_INVALID_OPERATION;
    }
    if (mPlayer->start() == 0) {
        currentState = MEDIA_PLAYER_STARTED;
        return VE_OK;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::stop() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED && currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
        return VE_INVALID_OPERATION;
    }
    if (mPlayer->stop() == 0) {
        currentState = MEDIA_PLAYER_STOPPED;
        return VE_OK;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::pause() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (currentState != MEDIA_PLAYER_STARTED) {
        return VE_INVALID_OPERATION;
    }
    if (mPlayer->pause() == 0) {
        currentState = MEDIA_PLAYER_PAUSED;
        return VE_OK;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return VE_UNKNOWN_ERROR;
}

VEResult VEPlayerDriver::reset() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mPlayer->reset() == 0) {
        currentState = MEDIA_PLAYER_IDLE;
        return VE_OK;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return VE_UNKNOWN_ERROR;
}

int64_t VEPlayerDriver::getDuration() {
    std::lock_guard<std::mutex> lk(mMutex);
    return mPlayer->getDuration();
}

VEResult VEPlayerDriver::setLooping(bool looping) {
    std::lock_guard<std::mutex> lk(mMutex);
    mPlayer->setLooping(looping);
    return 0;
}

VEResult VEPlayerDriver::setSpeedRate(float speed) {
    std::lock_guard<std::mutex> lk(mMutex);
    mPlayer->setPlaySpeed(speed);
    return 0;
}

VEResult VEPlayerDriver::setListener(std::shared_ptr<MediaPlayerListener> listener) {
    std::lock_guard<std::mutex> lk(mMutex);
    mListener = listener;
    return 0;
}

VEResult VEPlayerDriver::seekTo(double timestampMs) {
    std::lock_guard<std::mutex> lk(mMutex);
    ALOGI("VEPlayerDriver::%s timestampMs:%f, currentState:%d", __FUNCTION__, timestampMs, currentState);
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED && currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
        ALOGE("VEPlayerDriver::%s Invalid state for seekTo: %d", __FUNCTION__, currentState);
        return VE_INVALID_OPERATION;
    }

    if(mIsSeeking){
        ALOGI("VEPlayerDriver::%s timestampMs:%f drop seek", __FUNCTION__, timestampMs);
        return VE_INVALID_OPERATION;
    }
    mIsSeeking = true;


    if (currentState == MEDIA_PLAYER_STARTED) {
        mPlayer->pause();
    }

    ALOGI("VEPlayerDriver::%s timestampMs:%f exe seek", __FUNCTION__, timestampMs);
    int result = mPlayer->seek(timestampMs);
    if (result == 0) {

    } else {
        currentState = MEDIA_PLAYER_STATE_ERROR;
    }
    return result;
}

void VEPlayerDriver::notifyListener(int msg, int ext1, double ext2, const void *obj) {
    if (mListener) {
        mListener->notify(msg, ext1, ext2, obj);
    }
}
