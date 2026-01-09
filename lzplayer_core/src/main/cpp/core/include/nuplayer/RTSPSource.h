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

#ifndef RTSP_SOURCE_H_

#define RTSP_SOURCE_H_

#include "NuPlayerSource.h"

#include <mpeg2ts/ATSParser.h>

namespace android {

struct NuPlayer::RTSPSource : public NuPlayer::Source {
    RTSPSource(
            const std::shared_ptr<AMessage> &notify,
            const std::shared_ptr<IMediaHTTPService> &httpService,
            const char *url,
            const std::unordered_map<std::string, std::string> *headers,
            bool uidValid = false,
            uid_t uid = 0,
            bool isSDP = false);

    virtual status_t getBufferingSettings(
            BufferingSettings* buffering /* nonnull */) override;
    virtual status_t setBufferingSettings(const BufferingSettings& buffering) override;

    virtual void prepareAsync();
    virtual void start();
    virtual void stop();

    virtual status_t feedMoreTSData();

    virtual status_t dequeueAccessUnit(bool audio, std::shared_ptr<ABuffer> *accessUnit);

    virtual status_t getDuration(int64_t *durationUs);
    virtual status_t seekTo(
            int64_t seekTimeUs,
            MediaPlayerSeekMode mode = MediaPlayerSeekMode::SEEK_PREVIOUS_SYNC) override;

    void onMessageReceived(const std::shared_ptr<AMessage> &msg);

protected:
    virtual ~RTSPSource();

    virtual std::shared_ptr<MetaData> getFormatMeta(bool audio);

private:
    enum {
        kWhatNotify          = 'noti',
        kWhatDisconnect      = 'disc',
        kWhatPerformSeek     = 'seek',
        kWhatPollBuffering   = 'poll',
        kWhatSignalEOS       = 'eos ',
    };

    enum State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        SEEKING,
    };

    enum Flags {
        // Don't log any URLs.
        kFlagIncognito = 1,
    };

    struct TrackInfo {
        std::shared_ptr<AnotherPacketSource> mSource;

        int32_t mTimeScale;
        uint32_t mRTPTime;
        int64_t mNormalPlaytimeUs;
        bool mNPTMappingValid;
    };

    std::shared_ptr<IMediaHTTPService> mHTTPService;
    std::string mURL;
    std::unordered_map<std::string, std::string> mExtraHeaders;
    bool mUIDValid;
    uid_t mUID;
    uint32_t mFlags;
    bool mIsSDP;
    State mState;
    status_t mFinalResult;
    std::shared_ptr<AReplyToken> mDisconnectReplyID;
    std::mutex mBufferingLock;
    bool mBuffering;
    bool mInPreparationPhase;
    bool mEOSPending;

    std::mutex mBufferingSettingsLock;
    BufferingSettings mBufferingSettings;

    std::shared_ptr<ALooper> mLooper;
    std::shared_ptr<MyHandler> mHandler;
    std::shared_ptr<SDPLoader> mSDPLoader;

    std::vector<TrackInfo> mTracks;
    std::shared_ptr<AnotherPacketSource> mAudioTrack;
    std::shared_ptr<AnotherPacketSource> mVideoTrack;

    std::shared_ptr<ATSParser> mTSParser;

    int32_t mSeekGeneration;

    int64_t mEOSTimeoutAudio;
    int64_t mEOSTimeoutVideo;

    std::shared_ptr<AReplyToken> mSeekReplyID;

    std::shared_ptr<AnotherPacketSource> getSource(bool audio);

    void onConnected();
    void onSDPLoaded(const std::shared_ptr<AMessage> &msg);
    void onDisconnected(const std::shared_ptr<AMessage> &msg);
    void finishDisconnectIfPossible();

    void performSeek(int64_t seekTimeUs);
    void schedulePollBuffering();
    void checkBuffering(
            bool *prepared,
            bool *underflow,
            bool *overflow,
            bool *startServer,
            bool *finished);
    void onPollBuffering();

    bool haveSufficientDataOnAllTracks();

    void setEOSTimeout(bool audio, int64_t timeout);
    void setError(status_t err);
    void startBufferingIfNecessary();
    bool stopBufferingIfNecessary();
    void finishSeek(status_t err);

    void postSourceEOSIfNecessary();
    void signalSourceEOS(status_t result);
    void onSignalEOS(const std::shared_ptr<AMessage> &msg);

    bool sourceNearEOS(bool audio);
    bool sourceReachedEOS(bool audio);

    DISALLOW_EVIL_CONSTRUCTORS(RTSPSource);
};

}  // namespace android

#endif  // RTSP_SOURCE_H_
