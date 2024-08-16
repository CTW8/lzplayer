//
// Created by 李振 on 2024/7/25.
//

#ifndef LZPLAYER_VEPLAYERDIRVER_H
#define LZPLAYER_VEPLAYERDIRVER_H

#include "VEPlayer.h"
#include <android/native_window_jni.h>

class MediaPlayerListener {
public:
    virtual void notify(int msg, int ext1, int ext2, const void *obj) = 0;
};

class VEPlayerDirver {
public:
    VEPlayerDirver();
    ~VEPlayerDirver();

    int setDataSource(std::string path);
    int setSurface(ANativeWindow * win,int width,int height);
    int prepare();
    int prepareAsync();
    int start();
    int stop();
    int pause();
    int resume();
    int64_t getDuration();
    int setLooping(bool looping);
    int setSpeedRate(float speed);
    int setListener(std::shared_ptr<MediaPlayerListener> listener);
    int seekTo(int64_t timestamp);

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
    std::shared_ptr<VEPlayer> mPlayer;
    std::shared_ptr<MediaPlayerListener> mListener;

    void notifyListener(int msg, int ext1, int ext2, const void *obj);
};


#endif //LZPLAYER_VEPLAYERDIRVER_H
