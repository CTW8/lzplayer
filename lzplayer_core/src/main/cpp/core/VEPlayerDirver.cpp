//
// Created by 李振 on 2024/7/25.
//

#include "VEPlayerDirver.h"
#include "VEDef.h"

VEPlayerDirver::VEPlayerDirver()
        : currentState(MEDIA_PLAYER_IDLE), mPlayer(std::make_shared<VEPlayer>()) {

    mPlayerLooper = std::make_shared<ALooper>();
    mPlayerLooper->setName("player_thread");
    mPlayerLooper->start(false);
    mPlayerLooper->registerHandler(mPlayer);


    mPlayer->setOnInfoListener([this](int type, int msg1, double msg2, std::string msg3, void *msg4) {
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_INFO, msg1, static_cast<int>(msg2), msg4);
    });

    mPlayer->setOnProgressListener([this](double progress) {
        ALOGD("setOnProgressListener progress:%f",progress);
        notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS, 0, progress, nullptr); // Assuming 0 is the progress type
    });
}

VEPlayerDirver::~VEPlayerDirver() {}

int VEPlayerDirver::setDataSource(std::string path) {
    if (currentState != MEDIA_PLAYER_IDLE) {
        return -1; // Error: Invalid state
    }
    if (mPlayer->setDataSource(path) == 0) {
        currentState = MEDIA_PLAYER_INITIALIZED;
        return 0;
    }
    return -1;
}

int VEPlayerDirver::setSurface(ANativeWindow *win,int width,int height) {
//    if (currentState != MEDIA_PLAYER_INITIALIZED && currentState != MEDIA_PLAYER_PREPARED) {
//        return -1; // Error: Invalid state
//    }
    if (mPlayer->setDisplayOut(win, width, height) == 0) { // Assuming 0 width and height for simplicity
        return 0;
    }
    return -1;
}

int VEPlayerDirver::prepare() {
    if (currentState != MEDIA_PLAYER_INITIALIZED) {
        return -1; // Error: Invalid state
    }
    if (mPlayer->prepare() == 0) {
        currentState = MEDIA_PLAYER_PREPARED;
        return 0;
    }
    return -1;
}

int VEPlayerDirver::start() {
    if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_PAUSED) {
        return -1; // Error: Invalid state
    }
    if (mPlayer->start() == 0) {
        currentState = MEDIA_PLAYER_STARTED;
        return 0;
    }
    return -1;
}

int VEPlayerDirver::stop() {
    if (currentState != MEDIA_PLAYER_STARTED && currentState != MEDIA_PLAYER_PAUSED) {
        return -1; // Error: Invalid state
    }
    if (mPlayer->stop() == 0) {
        currentState = MEDIA_PLAYER_STOPPED;
        return 0;
    }
    return -1;
}

int VEPlayerDirver::pause() {
    if (currentState != MEDIA_PLAYER_STARTED) {
        return -1; // Error: Invalid state
    }
    if (mPlayer->pause() == 0) {
        currentState = MEDIA_PLAYER_PAUSED;
        return 0;
    }
    return -1;
}

int VEPlayerDirver::resume() {
    if (currentState != MEDIA_PLAYER_PAUSED) {
        return -1; // Error: Invalid state
    }
    if (mPlayer->resume() == 0) {
        currentState = MEDIA_PLAYER_STARTED;
        return 0;
    }
    return -1;
}

int64_t VEPlayerDirver::getDuration() {
    return mPlayer->getDuration();
}

int VEPlayerDirver::setLooping(bool looping) {
    mPlayer->setLooping(looping);
    return 0;
}

int VEPlayerDirver::setSpeedRate(float speed) {
    // Assume VEPlayer has a method to set speed rate, this is a placeholder
     mPlayer->setSpeedRate(speed);
    return 0;
}

int VEPlayerDirver::setListener(std::shared_ptr<MediaPlayerListener> listener) {
    mListener = listener;
    return 0;
}

int VEPlayerDirver::seekTo(double timestampMs) {
    if (currentState != MEDIA_PLAYER_STARTED && currentState != MEDIA_PLAYER_PAUSED) {
        return -1; // Error: Invalid state
    }
    return mPlayer->seek(timestampMs);
}

void VEPlayerDirver::notifyListener(int msg, int ext1, double ext2, const void *obj) {
    if (mListener) {
        mListener->notify(msg, ext1, ext2, obj);
    }
}

int VEPlayerDirver::prepareAsync() {
    return 0;
}
