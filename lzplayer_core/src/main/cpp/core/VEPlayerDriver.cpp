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
    if (currentState != MEDIA_PLAYER_IDLE) {
        return -1;
    }
    if (mPlayer->setDataSource(path) == 0) {
        currentState = MEDIA_PLAYER_INITIALIZED;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return -1;
}

VEResult VEPlayerDriver::setSurface(ANativeWindow *win, int width, int height) {
    if (mPlayer->setDisplayOut(win, width, height) == 0) {
        return 0;
    }
    return -1;
}

VEResult VEPlayerDriver::prepare() {
    if (currentState != MEDIA_PLAYER_STOPPED && currentState != MEDIA_PLAYER_INITIALIZED) {
        ALOGD("VEPlayerDriver::%s player status not correct!!!",__FUNCTION__ );
        return VE_UNKNOWN_ERROR;
    }
    std::unique_lock<std::mutex> lk(mMutex);
    mPlayer->prepare();

    mCond.wait(lk);

    return VE_OK;
}

VEResult VEPlayerDriver::prepareAsync() {
    if (currentState != MEDIA_PLAYER_STOPPED && currentState != MEDIA_PLAYER_INITIALIZED) {
        ALOGE("Invalid state for prepareAsync: %d", currentState);
        return -1;
    }
    if (mPlayer->prepareAsync() == 0) {
        currentState = MEDIA_PLAYER_PREPARING;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return -1;
}

VEResult VEPlayerDriver::start() {
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
        return -1;
    }
    if (mPlayer->start() == 0) {
        currentState = MEDIA_PLAYER_STARTED;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return -1;
}

VEResult VEPlayerDriver::stop() {
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED && currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
        return -1;
    }
    if (mPlayer->stop() == 0) {
        currentState = MEDIA_PLAYER_STOPPED;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return -1;
}

VEResult VEPlayerDriver::pause() {
    if (currentState != MEDIA_PLAYER_STARTED) {
        return -1;
    }
    if (mPlayer->pause() == 0) {
        currentState = MEDIA_PLAYER_PAUSED;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return -1;
}

VEResult VEPlayerDriver::reset() {
    if (mPlayer->reset() == 0) {
        currentState = MEDIA_PLAYER_IDLE;
        return 0;
    }
    currentState = MEDIA_PLAYER_STATE_ERROR;
    return -1;
}

int64_t VEPlayerDriver::getDuration() {
    return mPlayer->getDuration();
}

VEResult VEPlayerDriver::setLooping(bool looping) {
    mPlayer->setLooping(looping);
    return 0;
}

VEResult VEPlayerDriver::setSpeedRate(float speed) {
    mPlayer->setPlaySpeed(speed);
    return 0;
}

VEResult VEPlayerDriver::setListener(std::shared_ptr<MediaPlayerListener> listener) {
    mListener = listener;
    return 0;
}

VEResult VEPlayerDriver::seekTo(double timestampMs) {
    ALOGI("VEPlayerDriver::%s timestampMs:%f, currentState:%d", __FUNCTION__, timestampMs, currentState);
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED && currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
        ALOGE("VEPlayerDriver::%s Invalid state for seekTo: %d", __FUNCTION__, currentState);
        return -1;
    }

    {
        std::lock_guard<std::mutex> lk(mMutex);
        if(mIsSeeking){
            ALOGI("VEPlayerDriver::%s timestampMs:%f drop seek", __FUNCTION__, timestampMs);
            return VE_INVALID_OPERATION;
        }
        mIsSeeking = true;
    }

    if (currentState == MEDIA_PLAYER_STARTED) {
        mPlayer->pause();
    }

    ALOGI("VEPlayerDriver::%s timestampMs:%f exe seek", __FUNCTION__, timestampMs);
    int result = mPlayer->seek(timestampMs);
    if (result == 0) {
        if (currentState == MEDIA_PLAYER_STARTED) {
            mPlayer->start();
        } else if (currentState == MEDIA_PLAYER_PAUSED) {
            mPlayer->pause();
        }
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
