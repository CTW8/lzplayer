//
// Created by 李振 on 2024/7/25.
//

#ifndef LZPLAYER_VEPLAYERDRIVER_H
#define LZPLAYER_VEPLAYERDRIVER_H

#include "VEPlayer.h"
#include <android/native_window_jni.h>

class MediaPlayerListener {
public:
    virtual void notify(int msg, int ext1, double ext2, const void *obj) = 0;
};

class VEPlayerDriver {
public:
    VEPlayerDriver();
    ~VEPlayerDriver();

    VEResult setDataSource(std::string path);
    VEResult setSurface(ANativeWindow * win,int width,int height);

    VEResult prepare();
    VEResult prepareAsync();
    VEResult start();
    VEResult stop();
    VEResult pause();
    VEResult seekTo(double timestampMs);
    VEResult reset();

    int64_t  getDuration();
    VEResult setLooping(bool looping);
    VEResult setSpeedRate(float speed);
    VEResult setListener(std::shared_ptr<MediaPlayerListener> listener);

private:
    enum media_player_states {
        MEDIA_PLAYER_STATE_ERROR        = 0,
        MEDIA_PLAYER_IDLE               = 1 << 0,
        MEDIA_PLAYER_INITIALIZED        = 1 << 1,
        MEDIA_PLAYER_PREPARING          = 1 << 2,
        MEDIA_PLAYER_PREPARED           = 1 << 3,
        MEDIA_PLAYER_STARTED            = 1 << 4,
        MEDIA_PLAYER_PAUSED             = 1 << 5,
        MEDIA_PLAYER_STOPPED            = 1 << 6,
        MEDIA_PLAYER_PLAYBACK_COMPLETE  = 1 << 7
    };

    int currentState;
    std::shared_ptr<ALooper> mPlayerLooper = nullptr;
    std::shared_ptr<VEPlayer> mPlayer;
    std::shared_ptr<MediaPlayerListener> mListener;

    bool mIsSeeking = false;

    std::mutex mMutex;
    std::condition_variable mCond;

    void notifyListener(int msg, int ext1, double ext2, const void *obj);
};


#endif //LZPLAYER_VEPLAYERDRIVER_H
