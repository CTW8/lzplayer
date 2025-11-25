//
// Created by 李振 on 2024/7/25.
//
// VEPlayerDriver - NuPlayerDriver-style wrapper for VEPlayer
// Provides thread-safe interface and state machine management
//

#ifndef LZPLAYER_VEPLAYERDRIVER_H
#define LZPLAYER_VEPLAYERDRIVER_H

#include "VEPlayer.h"
#include "platform/VEPlatform.h"
#include "interface/IVideoRender.h"
#include "thread/ALooper.h"
#include <mutex>
#include <condition_variable>
#include <memory>

namespace VE {

/**
 * MediaPlayerListener - Interface for player event callbacks
 * Similar to Android's IMediaPlayerClient
 */
class MediaPlayerListener {
public:
    virtual void notify(int msg, int ext1, int ext2, const void *obj) = 0;
    virtual ~MediaPlayerListener() = default;
};

/**
 * VEPlayerDriver - NuPlayerDriver-style player driver
 * 
 * This class provides:
 * 1. Thread-safe wrapper around VEPlayer
 * 2. State machine management (similar to NuPlayerDriver)
 * 3. Synchronous/Asynchronous operation support
 * 4. Event notification to client
 */
class VEPlayerDriver {
public:
    // Player states (aligned with Android MediaPlayer states)
    enum State {
        STATE_IDLE,
        STATE_SET_DATASOURCE_PENDING,
        STATE_UNPREPARED,
        STATE_PREPARING,
        STATE_PREPARED,
        STATE_RUNNING,
        STATE_PAUSED,
        STATE_RESET_IN_PROGRESS,
        STATE_STOPPED,
        STATE_STOPPED_AND_PREPARING,
        STATE_STOPPED_AND_PREPARED,
    };

    VEPlayerDriver();
    ~VEPlayerDriver();

    // --- Client interface ---
    VEResult setListener(const std::shared_ptr<MediaPlayerListener> &listener);
    
    // Data source
    VEResult setDataSource(const std::string &path);
    
    // Surface
    VEResult setVideoSurfaceTexture(VENativeWindow win, int width, int height);
    
    // Prepare
    VEResult prepare();
    VEResult prepareAsync();
    
    // Playback control
    VEResult start();
    VEResult pause();
    VEResult stop();
    VEResult reset();
    
    // Seek
    VEResult seekTo(int64_t msec, int mode = 0);
    
    // Properties
    int64_t getCurrentPosition();
    int64_t getDuration();
    VEResult setLooping(bool looping);
    VEResult setPlaybackSettings(float rate);

    // Get current state
    State getState() const;

    // Legacy interface compatibility
    VEResult setSurface(VENativeWindow win, int width, int height) {
        return setVideoSurfaceTexture(win, width, height);
    }
    VEResult setSpeedRate(float speed) {
        return setPlaybackSettings(speed);
    }

private:
    // Internal listener class that bridges VEPlayer events to VEPlayerDriver
    class PlayerListener : public VEPlayer::Listener {
    public:
        explicit PlayerListener(VEPlayerDriver *driver) : mDriver(driver) {}
        void notify(int msg, int ext1, int ext2, const void *obj) override {
            if (mDriver) {
                mDriver->onPlayerNotify(msg, ext1, ext2, obj);
            }
        }
    private:
        VEPlayerDriver *mDriver;
    };
    
    // Called by PlayerListener
    void onPlayerNotify(int msg, int ext1, int ext2, const void *obj);
    
    // Internal helpers
    void notifyListener_l(int msg, int ext1, int ext2, const void *obj = nullptr);
    bool isValidStateForOperation_l(const char *operation) const;

    // Member variables
    mutable std::mutex mLock;
    std::condition_variable mCondition;
    
    State mState;
    bool mAtEOS;
    bool mLooping;
    bool mAutoLoop;
    
    // Duration and position
    int64_t mDurationUs;
    int64_t mPositionUs;
    
    // Seeking state
    bool mSeeking;
    int64_t mSeekInProgress;
    
    // Player and looper
    std::shared_ptr<VEPlayer> mPlayer;
    std::shared_ptr<ALooper> mLooper;
    std::shared_ptr<PlayerListener> mPlayerListener;
    
    // Client listener
    std::weak_ptr<MediaPlayerListener> mListener;
    
    // Playback rate
    float mPlaybackRate;
};

} // namespace VE

#endif //LZPLAYER_VEPLAYERDRIVER_H
