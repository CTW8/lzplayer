//
// Created by 李振 on 2024/7/25.
//
// VEPlayerDriver - NuPlayerDriver-style wrapper for VEPlayer
// Provides thread-safe interface and state machine management
// Reference: frameworks/av/media/libmediaplayerservice/nuplayer/NuPlayerDriver.h
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
 * 2. State machine management (aligned with NuPlayerDriver)
 * 3. Synchronous/Asynchronous operation support
 * 4. Event notification to client
 * 
 * State machine (from NuPlayerDriver):
 * IDLE -> SET_DATASOURCE_PENDING -> UNPREPARED
 * UNPREPARED -> PREPARING -> PREPARED
 * PREPARED -> RUNNING <-> PAUSED
 * RUNNING/PAUSED -> STOPPED
 * Any state -> RESET_IN_PROGRESS -> IDLE
 */
class VEPlayerDriver {
public:
    // Player states (aligned exactly with NuPlayerDriver::State)
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

    // --- Client interface (aligned with NuPlayerDriver) ---
    VEResult setListener(const std::shared_ptr<MediaPlayerListener> &listener);
    
    // Data source (NuPlayerDriver::setDataSource)
    VEResult setDataSource(const std::string &url);
    
    // Video surface (NuPlayerDriver::setVideoSurfaceTexture)
    VEResult setVideoSurfaceTexture(VENativeWindow surfaceTexture, int width, int height);
    
    // Prepare (NuPlayerDriver::prepare/prepareAsync)
    VEResult prepare();
    VEResult prepareAsync();
    
    // Playback control (NuPlayerDriver::start/pause/stop/reset)
    VEResult start();
    VEResult pause();
    VEResult stop();
    VEResult reset();
    
    // Seek (NuPlayerDriver::seekTo)
    // mode: SEEK_PREVIOUS_SYNC = 0, SEEK_NEXT_SYNC = 1, SEEK_CLOSEST_SYNC = 2, SEEK_CLOSEST = 3
    VEResult seekTo(int64_t msec, int mode = 0);
    
    // Properties (NuPlayerDriver getters/setters)
    VEResult getCurrentPosition(int64_t *msec);
    VEResult getDuration(int64_t *msec);
    VEResult setLooping(bool loop);
    bool isLooping();
    VEResult setPlaybackSettings(float rate);
    VEResult getPlaybackSettings(float *rate);

    // Get current state
    State getState() const;
    bool isPlaying();

    // Legacy interface compatibility
    VEResult setSurface(VENativeWindow win, int width, int height) {
        return setVideoSurfaceTexture(win, width, height);
    }
    VEResult setSpeedRate(float speed) {
        return setPlaybackSettings(speed);
    }
    int64_t getCurrentPosition() {
        int64_t msec = 0;
        getCurrentPosition(&msec);
        return msec;
    }
    int64_t getDuration() {
        int64_t msec = 0;
        getDuration(&msec);
        return msec;
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
    void notifySeekComplete_l();
    void notifyListener_l(int msg, int ext1 = 0, int ext2 = 0, const void *obj = nullptr);
    bool isValidStateForOperation_l(const char *operation) const;

    // Member variables
    mutable std::mutex mLock;
    std::condition_variable mCondition;
    
    State mState;
    bool mAtEOS;
    bool mLooping;
    bool mAutoLoop;
    
    // Duration and position (in microseconds internally, milliseconds externally)
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
    
    // Number of frames dropped
    int64_t mNumFramesTotal;
    int64_t mNumFramesDropped;
};

} // namespace VE

#endif //LZPLAYER_VEPLAYERDRIVER_H
