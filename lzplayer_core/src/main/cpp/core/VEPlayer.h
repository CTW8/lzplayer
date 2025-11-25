#ifndef __VEPLAYER__
#define __VEPLAYER__

#include <string>
#include "platform/VEPlatform.h"
#include "interface/IVideoRender.h"

#if VE_PLATFORM_ANDROID
#include <android/native_window_jni.h>
#include "jni.h"
#endif

#include "VEDemux.h"
#include "VEAudioDecoder.h"
#include "VEVideoDecoder.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEPacketQueue.h"
#include "VEFrameQueue.h"
#include "VEError.h"
#include "thread/AHandler.h"
#include "VEVideoRender.h"
#include "AudioOpenSLESOutput.h"
#include "VEDef.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"

namespace VE {

    // Forward declarations
    class VESource;
    class VERenderer;

    /**
     * VEPlayer - NuPlayer-style media player implementation
     * 
     * Architecture follows Android NuPlayer design:
     * - Source: Media source abstraction (similar to GenericSource)
     * - Decoders: Audio/Video decoders
     * - Renderer: Audio/Video rendering and synchronization
     * 
     * All operations are async via AHandler/ALooper message passing.
     */
    class VEPlayer : public AHandler {
    public:
        // Listener interface for player events (similar to NuPlayerDriver::notifyListener)
        struct Listener {
            virtual void notify(int msg, int ext1, int ext2, const void *obj) = 0;
            virtual ~Listener() = default;
        };

        VEPlayer();
        ~VEPlayer();

        // --- Driver Interface (called from VEPlayerDriver) ---
        void setListener(const std::shared_ptr<Listener> &listener);

        // Data source operations
        VEResult setDataSource(const std::string &path);
        VEResult setVideoSurfaceTexture(VENativeWindow win, int width, int height);

        // Playback control
        VEResult prepare();
        VEResult prepareAsync();
        VEResult start();
        VEResult pause();
        VEResult resume();
        VEResult stop();
        VEResult resetAsync();

        // Seek operation
        VEResult seekTo(int64_t seekTimeUs, int mode = 0 /* SEEK_PREVIOUS_SYNC */);

        // Properties
        int64_t getCurrentPosition();
        int64_t getDuration();
        VEResult setPlaybackSettings(float rate);
        VEResult setLooping(bool looping);

    private:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        // --- Internal message handlers ---
        void onSetDataSource(const std::shared_ptr<AMessage> &msg);
        void onPrepareAsync();
        void onStart();
        void onPause();
        void onResume();
        void onStop();
        void onReset();
        void onSeek(const std::shared_ptr<AMessage> &msg);
        void onSetVideoSurface(const std::shared_ptr<AMessage> &msg);

        // Source/Decoder/Renderer event handlers
        void onSourceNotify(const std::shared_ptr<AMessage> &msg);
        void onDecoderNotify(const std::shared_ptr<AMessage> &msg);
        void onRendererNotify(const std::shared_ptr<AMessage> &msg);

        // Helper methods
        void performSeek(int64_t seekTimeUs, int mode);
        void finishSeek();
        void notifyListener(int msg, int ext1, int ext2, const void *obj = nullptr);
        void finishFlushIfPossible();
        void tryOpenAudioSink();
        void instantiateDecoder(bool audio, std::shared_ptr<AMessage> *format);
        void finishPrepare();
        void handleEOS();

        // --- Message types (aligned with NuPlayer) ---
        enum {
            kWhatSetDataSource          = 'sDS ',
            kWhatPrepare                = 'prep',
            kWhatSetVideoSurface        = 'sVSu',
            kWhatStart                  = 'strt',
            kWhatPause                  = 'paus',
            kWhatResume                 = 'rsme',
            kWhatStop                   = 'stop',
            kWhatReset                  = 'rset',
            kWhatSeek                   = 'seek',
            kWhatSourceNotify           = 'srcN',
            kWhatVideoNotify            = 'vidN',
            kWhatAudioNotify            = 'audN',
            kWhatRendererNotify         = 'renN',
            kWhatClosedCaptionNotify    = 'capN',
            kWhatScanSources            = 'scan',
            kWhatFlush                  = 'flus',
            kWhatPerformSeek            = 'prSk',
        };

        // --- Flush state (NuPlayer style) ---
        enum FlushStatus {
            NONE,
            FLUSHING_DECODER,
            FLUSHING_DECODER_SHUTDOWN,
            SHUTTING_DOWN_DECODER,
            FLUSHED,
            SHUT_DOWN,
        };

        // --- Member variables ---
        std::weak_ptr<Listener> mListener;

        // Source - handles media demuxing (similar to GenericSource)
        std::shared_ptr<VEDemux> mSource;
        std::shared_ptr<ALooper> mSourceLooper;

        // Decoders
        std::shared_ptr<VEAudioDecoder> mAudioDecoder;
        std::shared_ptr<ALooper> mAudioDecoderLooper;
        std::shared_ptr<VEVideoDecoder> mVideoDecoder;
        std::shared_ptr<ALooper> mVideoDecoderLooper;

        // Renderer - handles audio/video output and sync
        std::shared_ptr<VEAudioRender> mAudioRenderer;
        std::shared_ptr<ALooper> mAudioRendererLooper;
        std::shared_ptr<VEVideoDisplay> mVideoRenderer;
        std::shared_ptr<ALooper> mVideoRendererLooper;

        // AV sync controller
        std::shared_ptr<VEAVsync> mAVSync;

        // Notification message for components
        std::shared_ptr<AMessage> mNotifyMsg;

        // Media info
        std::shared_ptr<VEMediaInfo> mMediaInfo;

        // Data source path
        std::string mDataSourcePath;

        // Surface info
        VENativeWindow mNativeWindow = nullptr;
        int mSurfaceWidth = 0;
        int mSurfaceHeight = 0;

        // Playback state
        bool mStarted = false;
        bool mPaused = false;
        bool mPausedByClient = false;
        bool mSourceStarted = false;

        // Flush state
        FlushStatus mFlushingAudio = NONE;
        FlushStatus mFlushingVideo = NONE;

        // EOS tracking
        bool mAudioEOS = false;
        bool mVideoEOS = false;
        bool mSentEOS = false;

        // Seek state
        bool mSeeking = false;
        int64_t mSeekTimeUs = -1;
        int mSeekMode = 0;

        // Looping
        bool mLooping = false;

        // Timestamps
        int64_t mPendingAudioAccessUnitTimeUs = -1;
        int64_t mPendingVideoAccessUnitTimeUs = -1;

        // Synchronization
        mutable std::mutex mLock;

        DISALLOW_EVIL_CONSTRUCTORS(VEPlayer);
    };

} // namespace VE

// Macro definition for disallowing copy/assign
#ifndef DISALLOW_EVIL_CONSTRUCTORS
#define DISALLOW_EVIL_CONSTRUCTORS(name) \
    name(const name&) = delete; \
    name& operator=(const name&) = delete;
#endif

#endif // __VEPLAYER__