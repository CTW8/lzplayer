//
// VEDecoderBase.h
// NuPlayer-style DecoderBase abstract class
// Reference: frameworks/av/media/libmediaplayerservice/nuplayer/NuPlayerDecoderBase.h
//

#ifndef LZPLAYER_VE_DECODER_BASE_H
#define LZPLAYER_VE_DECODER_BASE_H

#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEMediaDef.h"
#include "VEDemux.h"
#include <memory>

namespace VE {

/**
 * VEDecoderBase - Abstract base class for audio/video decoders
 * 
 * This follows NuPlayer::DecoderBase design pattern:
 * - Async message-based operations via AHandler
 * - Unified notification interface back to VEPlayer
 * - Common decoder lifecycle: configure -> setRenderer -> start -> pause/resume -> flush -> shutdown
 */
class VEDecoderBase : public AHandler {
public:
    // Notification messages sent to VEPlayer (NuPlayer::DecoderBase style)
    enum {
        kWhatInputDiscontinuity  = 'inDi',
        kWhatVideoSizeChanged    = 'viSC',
        kWhatFlushCompleted      = 'fluC',
        kWhatShutdownCompleted   = 'shuC',
        kWhatResumeCompleted     = 'resC',
        kWhatEOS                 = 'eos ',
        kWhatError               = 'err ',
    };

    VEDecoderBase(const std::shared_ptr<AMessage> &notify);
    virtual ~VEDecoderBase();

    // --- NuPlayer::DecoderBase interface ---
    
    // Configure decoder with source (similar to configure(sp<AMessage>))
    void configure(const std::shared_ptr<VEDemux> &source);
    
    // Set renderer for decoded output
    void setRenderer(const std::shared_ptr<AHandler> &renderer);
    
    // Playback control
    void init();
    void start();
    void pause();
    void resume();
    void flush();
    void shutdown();
    
    // Get decoder type (audio or video)
    virtual bool isAudio() const = 0;

protected:
    // Internal message types
    enum {
        kWhatConfigure           = 'conf',
        kWhatSetRenderer         = 'setR',
        kWhatStart               = 'star',
        kWhatPause               = 'paus',
        kWhatResume              = 'rsme',
        kWhatFlush               = 'flus',
        kWhatShutdown            = 'shut',
        kWhatDecode              = 'deco',
        kWhatInit                = 'init',
    };

    // Message handler - subclasses implement the actual decoding logic
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

    // Abstract methods for subclass implementation
    virtual void onConfigure(const std::shared_ptr<VEDemux> &source) = 0;
    virtual void onSetRenderer(const std::shared_ptr<AHandler> &renderer) = 0;
    virtual void onStart() = 0;
    virtual void onPause() = 0;
    virtual void onResume() = 0;
    virtual void onFlush() = 0;
    virtual void onShutdown() = 0;
    virtual void onDecode() = 0;
    virtual void onInit() = 0;

    // Helper to post notification back to VEPlayer
    void notifyFlushComplete();
    void notifyShutdownComplete();
    void notifyResumeComplete();
    void notifyEOS();
    void notifyError(int err);

    // Members
    std::shared_ptr<AMessage> mNotify;
    std::shared_ptr<VEDemux> mSource;
    std::shared_ptr<AHandler> mRenderer;
    
    bool mPaused = false;
    bool mStarted = false;
    bool mFlushing = false;
    bool mShuttingDown = false;

    DISALLOW_EVIL_CONSTRUCTORS(VEDecoderBase);
};

} // namespace VE

#endif // LZPLAYER_VE_DECODER_BASE_H
