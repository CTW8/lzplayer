//
// VERenderer.h
// NuPlayer-style unified Renderer for audio and video
// Reference: frameworks/av/media/libmediaplayerservice/nuplayer/NuPlayerRenderer.h
//

#ifndef LZPLAYER_VE_RENDERER_H
#define LZPLAYER_VE_RENDERER_H

#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEMediaDef.h"
#include "VEFrame.h"
#include "VEAVsync.h"
#include "IAudioRender.h"
#include "IVideoRender.h"
#include "platform/VEPlatform.h"
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>

namespace VE {

/**
 * VERenderer - Unified audio/video renderer with A/V sync
 * 
 * This follows NuPlayer::Renderer design pattern:
 * - Handles both audio and video rendering in one class
 * - Manages audio sink (OpenSL ES) and video output (OpenGL ES)
 * - Handles A/V synchronization using audio as master clock
 * - Queue-based rendering with proper timing
 * 
 * Key NuPlayer::Renderer concepts:
 * - Audio queue and video queue for decoded frames
 * - Audio callback-driven playback
 * - Video frame scheduling based on audio clock
 * - Flush/pause/resume synchronization
 */
class VERenderer : public AHandler {
public:
    // Notification messages sent to VEPlayer (NuPlayer::Renderer style)
    enum {
        kWhatEOS                 = 'eos ',
        kWhatFlushComplete       = 'fluC',
        kWhatVideoRenderingStart = 'vidS',
        kWhatAudioOffloadTearDown = 'aoTD',
        kWhatAudioTearDown       = 'auTD',
        kWhatPosition            = 'posi',
    };

    VERenderer(const std::shared_ptr<AMessage> &notify,
               const std::shared_ptr<VEAVsync> &avSync);
    virtual ~VERenderer();

    // --- NuPlayer::Renderer interface ---

    // Set native window for video rendering
    void setVideoSurface(VENativeWindow nativeWindow, int width, int height);

    // Open audio sink with format info
    void openAudioSink(int sampleRate, int channelCount, int format);
    
    // Close audio sink
    void closeAudioSink();

    // Queue buffer for rendering (NuPlayer: queueBuffer)
    void queueBuffer(bool audio, const std::shared_ptr<VEFrame> &buffer);

    // Queue EOS marker (NuPlayer: queueEOS)
    void queueEOS(bool audio);

    // Playback control
    void start();
    void pause();
    void resume();
    void flush(bool audio, bool video);

    // Get current media time (for A/V sync)
    int64_t getMediaTimeUs();

    // Set playback rate
    void setPlaybackRate(float rate);

protected:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

private:
    // Internal message types (NuPlayer::Renderer style)
    enum {
        kWhatOpenAudioSink       = 'opnA',
        kWhatCloseAudioSink      = 'clsA',
        kWhatSetVideoSurface     = 'setV',
        kWhatStart               = 'star',
        kWhatPause               = 'paus',
        kWhatResume              = 'rsme',
        kWhatFlush               = 'flus',
        kWhatQueueBuffer         = 'queB',
        kWhatQueueEOS            = 'queE',
        kWhatDrainAudioQueue     = 'draA',
        kWhatDrainVideoQueue     = 'draV',
        kWhatPostDrainVideoQueue = 'pdVQ',
        kWhatSetRate             = 'rate',
    };

    // Message handlers
    void onOpenAudioSink(const std::shared_ptr<AMessage> &msg);
    void onCloseAudioSink();
    void onSetVideoSurface(const std::shared_ptr<AMessage> &msg);
    void onStart();
    void onPause();
    void onResume();
    void onFlush(const std::shared_ptr<AMessage> &msg);
    void onQueueBuffer(const std::shared_ptr<AMessage> &msg);
    void onQueueEOS(const std::shared_ptr<AMessage> &msg);
    void onDrainAudioQueue();
    void onDrainVideoQueue();
    void onSetRate(const std::shared_ptr<AMessage> &msg);

    // Rendering helpers
    void postDrainAudioQueue();
    void postDrainVideoQueue();
    int64_t getDrainTimeUs(bool audio, const std::shared_ptr<VEFrame> &frame);
    bool renderAudioFrame(const std::shared_ptr<VEFrame> &frame);
    bool renderVideoFrame(const std::shared_ptr<VEFrame> &frame);

    // Notification helpers
    void notifyEOS(bool audio);
    void notifyFlushComplete(bool audio);
    void notifyPosition(int64_t positionUs);
    void notifyVideoRenderingStart();

    // Members
    std::shared_ptr<AMessage> mNotify;
    std::shared_ptr<VEAVsync> mAVSync;

    // Audio sink
    std::shared_ptr<IAudioRender> mAudioSink;
    int mSampleRate = 0;
    int mChannelCount = 0;
    int mAudioFormat = 0;

    // Video surface
    std::shared_ptr<IVideoRender> mVideoSink;
    VENativeWindow mNativeWindow = nullptr;
    int mVideoWidth = 0;
    int mVideoHeight = 0;

    // Queues (NuPlayer::Renderer style)
    std::deque<std::shared_ptr<VEFrame>> mAudioQueue;
    std::deque<std::shared_ptr<VEFrame>> mVideoQueue;
    std::mutex mQueueLock;

    // State
    bool mPaused = false;
    bool mStarted = false;
    bool mAudioEOS = false;
    bool mVideoEOS = false;
    bool mAudioEOSGenerated = false;
    bool mVideoEOSGenerated = false;
    bool mVideoRenderingStarted = false;

    // Timing
    int64_t mAnchorTimeMediaUs = -1;
    int64_t mAnchorTimeRealUs = -1;
    int64_t mAnchorNumFramesWritten = 0;
    float mPlaybackRate = 1.0f;

    // Draining flags
    bool mDrainingAudio = false;
    bool mDrainingVideo = false;

    DISALLOW_EVIL_CONSTRUCTORS(VERenderer);
};

} // namespace VE

#endif // LZPLAYER_VE_RENDERER_H
