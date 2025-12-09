/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NU_PLAYER_H_

#define NU_PLAYER_H_

#include <memory>
#include "MediaPlayerInterface.h"
#include "AHandler.h"

namespace android {

class NuPlayer : public AHandler {

public:
    explicit NuPlayer(pid_t pid, const std::shared_ptr<MediaClock> &mediaClock);

    void setUID(uid_t uid);

    void init(const std::weak_ptr<NuPlayerDriver> &driver);

    void setDataSourceAsync(const std::shared_ptr<IStreamSource> &source);

    void setDataSourceAsync(
            const std::shared_ptr<IMediaHTTPService> &httpService,
            const char *url,
            const KeyedVector<std::string, std::string> *headers);

    void setDataSourceAsync(int fd, int64_t offset, int64_t length);

    void setDataSourceAsync(const std::shared_ptr<DataSource> &source);

    void setDataSourceAsync(const std::string& rtpParams);

    status_t getBufferingSettings(BufferingSettings* buffering /* nonnull */);
    status_t setBufferingSettings(const BufferingSettings& buffering);

    void prepareAsync();

    void setVideoSurfaceTextureAsync(
            const std::shared_ptr<IGraphicBufferProducer> &bufferProducer);

    void setAudioSink(const std::shared_ptr<MediaPlayerBase::AudioSink> &sink);
    status_t setPlaybackSettings(const AudioPlaybackRate &rate);
    status_t getPlaybackSettings(AudioPlaybackRate *rate /* nonnull */);
    status_t setSyncSettings(const AVSyncSettings &sync, float videoFpsHint);
    status_t getSyncSettings(AVSyncSettings *sync /* nonnull */, float *videoFps /* nonnull */);

    void start();

    void pause();

    // Will notify the driver through "notifyResetComplete" once finished.
    void resetAsync();

    // Request a notification when specified media time is reached.
    status_t notifyAt(int64_t mediaTimeUs);

    // Will notify the driver through "notifySeekComplete" once finished
    // and needNotify is true.
    void seekToAsync(
            int64_t seekTimeUs,
            MediaPlayerSeekMode mode = MediaPlayerSeekMode::SEEK_PREVIOUS_SYNC,
            bool needNotify = false);

    status_t setVideoScalingMode(int32_t mode);
    status_t getTrackInfo(Parcel* reply) const;
    status_t getSelectedTrack(int32_t type, Parcel* reply) const;
    status_t selectTrack(size_t trackIndex, bool select, int64_t timeUs);
    status_t getCurrentPosition(int64_t *mediaUs);
    void getStats(std::vector<std::shared_ptr<AMessage> > *trackStats);

    std::shared_ptr<MetaData> getFileMeta();
    float getFrameRate();

    // Modular DRM
    status_t prepareDrm(const uint8_t uuid[16], const std::vector<uint8_t> &drmSessionId);
    status_t releaseDrm();

    const char *getDataSourceType();

    void updateInternalTimers();

    void setTargetBitrate(int bitrate /* bps */);

protected:
    virtual ~NuPlayer();

    virtual void onMessageReceived(const std::shared_ptr<AMessage> &msg);

public:
    struct NuPlayerStreamstd::listener;
    struct Source;

private:
    struct Decoder;
    struct DecoderBase;
    struct DecoderPassThrough;
    struct CCDecoder;
    struct GenericSource;
    struct HTTPLiveSource;
    struct Renderer;
    struct RTPSource;
    struct RTSPSource;
    struct StreamingSource;
    struct Action;
    struct SeekAction;
    struct SetSurfaceAction;
    struct ResumeDecoderAction;
    struct FlushDecoderAction;
    struct PostMessageAction;
    struct SimpleAction;

    enum {
        kWhatSetDataSource              = '=DaS',
        kWhatPrepare                    = 'prep',
        kWhatSetVideoSurface            = '=VSu',
        kWhatSetAudioSink               = '=AuS',
        kWhatMoreDataQueued             = 'more',
        kWhatConfigPlayback             = 'cfPB',
        kWhatConfigSync                 = 'cfSy',
        kWhatGetPlaybackSettings        = 'gPbS',
        kWhatGetSyncSettings            = 'gSyS',
        kWhatStart                      = 'strt',
        kWhatScanSources                = 'scan',
        kWhatVideoNotify                = 'vidN',
        kWhatAudioNotify                = 'audN',
        kWhatClosedCaptionNotify        = 'capN',
        kWhatRendererNotify             = 'renN',
        kWhatReset                      = 'rset',
        kWhatNotifyTime                 = 'nfyT',
        kWhatSeek                       = 'seek',
        kWhatPause                      = 'paus',
        kWhatResume                     = 'rsme',
        kWhatPollDuration               = 'polD',
        kWhatSourceNotify               = 'srcN',
        kWhatGetTrackInfo               = 'gTrI',
        kWhatGetSelectedTrack           = 'gSel',
        kWhatSelectTrack                = 'selT',
        kWhatGetBufferingSettings       = 'gBus',
        kWhatSetBufferingSettings       = 'sBuS',
        kWhatPrepareDrm                 = 'pDrm',
        kWhatReleaseDrm                 = 'rDrm',
        kWhatMediaClockNotify           = 'mckN',
    };

    std::weak_ptr<NuPlayerDriver> mDriver;
    bool mUIDValid;
    uid_t mUID;
    pid_t mPID;
    const std::shared_ptr<MediaClock> mMediaClock;
    std::mutex mSourceLock;  // guard |mSource|.
    std::shared_ptr<Source> mSource;
    uint32_t mSourceFlags;
    std::shared_ptr<Surface> mSurface;
    std::shared_ptr<MediaPlayerBase::AudioSink> mAudioSink;
    std::shared_ptr<DecoderBase> mVideoDecoder;
    bool mOffloadAudio;
    std::shared_ptr<DecoderBase> mAudioDecoder;
    std::mutex mDecoderLock;  // guard |mAudioDecoder| and |mVideoDecoder|.
    std::shared_ptr<CCDecoder> mCCDecoder;
    std::shared_ptr<Renderer> mRenderer;
    std::shared_ptr<ALooper> mRendererLooper;
    int32_t mAudioDecoderGeneration;
    int32_t mVideoDecoderGeneration;
    int32_t mRendererGeneration;

    std::mutex mPlayingTimeLock;
    int64_t mLastStartedPlayingTimeNs;
    void updatePlaybackTimer(bool stopping, const char *where);
    void startPlaybackTimer(const char *where);

    int64_t mLastStartedRebufferingTimeNs;
    void startRebufferingTimer();
    void updateRebufferingTimer(bool stopping, bool exitingPlayback);

    int64_t mPreviousSeekTimeUs;

    std::list<std::shared_ptr<Action>> mDeferredActions;

    bool mAudioEOS;
    bool mVideoEOS;

    bool mScanSourcesPending;
    int32_t mScanSourcesGeneration;

    int32_t mPollDurationGeneration;
    int32_t mTimedTextGeneration;

    enum FlushStatus {
        NONE,
        FLUSHING_DECODER,
        FLUSHING_DECODER_SHUTDOWN,
        SHUTTING_DOWN_DECODER,
        FLUSHED,
        SHUT_DOWN,
    };

    enum FlushCommand {
        FLUSH_CMD_NONE,
        FLUSH_CMD_FLUSH,
        FLUSH_CMD_SHUTDOWN,
    };

    // Status of flush responses from the decoder and renderer.
    bool mFlushComplete[2][2];

    FlushStatus mFlushingAudio;
    FlushStatus mFlushingVideo;

    // Status of flush responses from the decoder and renderer.
    bool mResumePending;

    int32_t mVideoScalingMode;

    AudioPlaybackRate mPlaybackSettings;
    AVSyncSettings mSyncSettings;
    float mVideoFpsHint;
    bool mStarted;
    bool mPrepared;
    bool mResetting;
    bool mSourceStarted;
    bool mAudioDecoderError;
    bool mVideoDecoderError;

    // Actual pause state, either as requested by client or due to buffering.
    bool mPaused;

    // Pause state as requested by client. Note that if mPausedByClient is
    // true, mPaused is always true; if mPausedByClient is false, mPaused could
    // still become true, when we pause internally due to buffering.
    bool mPausedByClient;

    // Pause state as requested by source (internally) due to buffering
    bool mPausedForBuffering;

    // Modular DRM
    typedef enum {
        DATA_SOURCE_TYPE_NONE,
        DATA_SOURCE_TYPE_HTTP_LIVE,
        DATA_SOURCE_TYPE_RTP,
        DATA_SOURCE_TYPE_RTSP,
        DATA_SOURCE_TYPE_GENERIC_URL,
        DATA_SOURCE_TYPE_GENERIC_FD,
        DATA_SOURCE_TYPE_MEDIA,
        DATA_SOURCE_TYPE_STREAM,
    } DATA_SOURCE_TYPE;

    std::atomic<DATA_SOURCE_TYPE> mDataSourceType;

    inline const std::shared_ptr<DecoderBase> &getDecoder(bool audio) {
        return audio ? mAudioDecoder : mVideoDecoder;
    }

    inline void clearFlushComplete() {
        mFlushComplete[0][0] = false;
        mFlushComplete[0][1] = false;
        mFlushComplete[1][0] = false;
        mFlushComplete[1][1] = false;
    }

    void tryOpenAudioSinkForOffload(
            const std::shared_ptr<AMessage> &format, const std::shared_ptr<MetaData> &audioMeta, bool hasVideo);
    void closeAudioSink();
    void restartAudio(
            int64_t currentPositionUs, bool forceNonOffload, bool needsToCreateAudioDecoder);
    void determineAudioModeChange(const std::shared_ptr<AMessage> &audioFormat);

    status_t instantiateDecoder(
            bool audio, std::shared_ptr<DecoderBase> *decoder, bool checkAudioModeChange = true);

    status_t onInstantiateSecureDecoders();

    void updateVideoSize(
            const std::shared_ptr<AMessage> &inputFormat,
            const std::shared_ptr<AMessage> &outputFormat = NULL);

    void notifystd::listener(int msg, int ext1, int ext2, const Parcel *in = NULL);

    void handleFlushComplete(bool audio, bool isDecoder);
    void finishFlushIfPossible();

    void onStart(
            int64_t startPositionUs = -1,
            MediaPlayerSeekMode mode = MediaPlayerSeekMode::SEEK_PREVIOUS_SYNC);
    void onResume();
    void onPause();

    bool audioDecoderStillNeeded();

    void flushDecoder(bool audio, bool needShutdown);

    void finishResume();
    void notifyDriverSeekComplete();

    void postScanSources();

    void schedulePollDuration();
    void cancelPollDuration();

    void processDeferredActions();

    void performSeek(int64_t seekTimeUs, MediaPlayerSeekMode mode);
    void performDecoderFlush(FlushCommand audio, FlushCommand video);
    void performReset();
    void performScanSources();
    void performSetSurface(const std::shared_ptr<Surface> &wrapper);
    void performResumeDecoders(bool needNotify);

    void onSourceNotify(const std::shared_ptr<AMessage> &msg);
    void onClosedCaptionNotify(const std::shared_ptr<AMessage> &msg);

    void queueDecoderShutdown(
            bool audio, bool video, const std::shared_ptr<AMessage> &reply);

    void sendSubtitleData(const std::shared_ptr<ABuffer> &buffer, int32_t baseIndex);
    void sendTimedMetaData(const std::shared_ptr<ABuffer> &buffer);
    void sendTimedTextData(const std::shared_ptr<ABuffer> &buffer);
    void sendIMSRxNotice(const std::shared_ptr<AMessage> &msg);
    
    status_t onPrepareDrm(const std::shared_ptr<AMessage> &msg);
    status_t onReleaseDrm();

    DISALLOW_EVIL_CONSTRUCTORS(NuPlayer);
};

}  // namespace android

#endif  // NU_PLAYER_H_
