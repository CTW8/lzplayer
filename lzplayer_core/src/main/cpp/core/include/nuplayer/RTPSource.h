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

#ifndef RTP_SOURCE_H_

#define RTP_SOURCE_H_


#include "NuPlayerSource.h"

namespace android {

struct ALooper;
struct AnotherPacketSource;

struct NuPlayer::RTPSource : public NuPlayer::Source {
    RTPSource(
            const std::shared_ptr<AMessage> &notify,
            const std::string& rtpParams);

    virtual status_t getBufferingSettings(
            BufferingSettings* buffering /* nonnull */) override;
    virtual status_t setBufferingSettings(const BufferingSettings& buffering) override;

    virtual void prepareAsync();
    virtual void start();
    virtual void stop();
    virtual void pause();
    virtual void resume();

    virtual status_t feedMoreTSData();

    virtual status_t dequeueAccessUnit(bool audio, std::shared_ptr<ABuffer> *accessUnit);

    virtual status_t getDuration(int64_t *durationUs);
    virtual status_t seekTo(
            int64_t seekTimeUs,
            MediaPlayerSeekMode mode = MediaPlayerSeekMode::SEEK_PREVIOUS_SYNC) override;

    virtual bool isRealTime() const;

    void onMessageReceived(const std::shared_ptr<AMessage> &msg);

    virtual void setTargetBitrate(int32_t bitrate) override;

protected:
    virtual ~RTPSource();

    virtual std::shared_ptr<MetaData> getFormatMeta(bool audio);

private:
    enum {
        kWhatAccessUnit = 'accU',
        kWhatAccessUnitComplete = 'accu',
        kWhatDisconnect = 'disc',
        kWhatEOS = 'eos!',
        kWhatPollBuffering = 'poll',
        kWhatSetBufferingSettings = 'sBuS',
    };

    const int64_t kBufferingPollIntervalUs = 1000000ll;

    enum State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        PAUSED,
    };

    struct TrackInfo {

        /* SDP of track */
        bool mIsAudio;
        int32_t mPayloadType;
        std::string mMimeType;
        std::string mCodecName;
        int32_t mCodecProfile;
        int32_t mCodecLevel;
        int32_t mWidth;
        int32_t mHeight;
        std::string mLocalIp;
        std::string mRemoteIp;
        int32_t mLocalPort;
        int32_t mRemotePort;
        int64_t mSocketNetwork;
        int32_t mTimeScale;
        int32_t mAS;

        /* RTP jitter buffer time in milliseconds */
        uint32_t mJbTimeMs;
        /* Unique ID indicates itself */
        uint32_t mSelfID;
        /* extmap:<value> for CVO will be set to here */
        int32_t mCVOExtMap;
        /* To check ECN is supported or not */
        int32_t mRtpSockOptEcn;

        /* a copy of TrackInfo in RTSPSource */
        std::shared_ptr<AnotherPacketSource> mSource;
        uint32_t mRTPTime;
        int64_t mNormalPlaytimeUs;
        bool mNPTMappingValid;

        /* a copy of TrackInfo in MyHandler.h */
        int mRTPSocket;
        int mRTCPSocket;
        uint32_t mFirstSeqNumInSegment;
        bool mNewSegment;
        int32_t mAllowedStaleAccessUnits;
        uint32_t mRTPAnchor;
        int64_t mNTPAnchorUs;
        bool mEOSReceived;
        uint32_t mNormalPlayTimeRTP;
        int64_t mNormalPlayTimeUs;
        std::shared_ptr<APacketSource> mPacketSource;
        std::list<std::shared_ptr<ABuffer>> mPackets;
    };

    const std::string mRTPParams;
    uint32_t mFlags;
    State mState;
    status_t mFinalResult;

    // below 3 parameters need to be checked whether it needed or not.
    std::mutex mBufferingLock;
    bool mBuffering;
    bool mInPreparationPhase;
    std::mutex mBufferingSettingsLock;
    BufferingSettings mBufferingSettings;

    std::shared_ptr<ALooper> mLooper;

    std::shared_ptr<ARTPConnection> mRTPConn;

    std::vector<TrackInfo> mTracks;
    std::shared_ptr<AnotherPacketSource> mAudioTrack;
    std::shared_ptr<AnotherPacketSource> mVideoTrack;

    int64_t mEOSTimeoutAudio;
    int64_t mEOSTimeoutVideo;

    /* MyHandler.h */
    bool mFirstAccessUnit;
    bool mAllTracksHaveTime;
    int64_t mNTPAnchorUs;
    int64_t mMediaAnchorUs;
    int64_t mLastMediaTimeUs;
    int64_t mNumAccessUnitsReceived;
    int32_t mLastCVOUpdated;
    bool mReceivedFirstRTCPPacket;
    bool mReceivedFirstRTPPacket;
    bool mPausing;
    int32_t mPauseGeneration;

    std::shared_ptr<AnotherPacketSource> getSource(bool audio);

    /* MyHandler.h */
    void onTimeUpdate(int32_t trackIndex, uint32_t rtpTime, uint64_t ntpTime);
    bool addMediaTimestamp(int32_t trackIndex, const TrackInfo *track,
            const std::shared_ptr<ABuffer> &accessUnit);
    bool dataReceivedOnAllChannels();
    void postQueueAccessUnit(size_t trackIndex, const std::shared_ptr<ABuffer> &accessUnit);
    void postQueueEOS(size_t trackIndex, status_t finalResult);
    std::shared_ptr<MetaData> getTrackFormat(size_t index, int32_t *timeScale);
    void onConnected();
    void onDisconnected(const std::shared_ptr<AMessage> &msg);

    void schedulePollBuffering();
    void onPollBuffering();

    bool haveSufficientDataOnAllTracks();

    void setEOSTimeout(bool audio, int64_t timeout);

    status_t setParameters(const std::string &params);
    status_t setParameter(const std::string &key, const std::string &value);
    void setSocketNetwork(int64_t networkHandle);
    static void TrimString(std::string *s);

    DISALLOW_EVIL_CONSTRUCTORS(RTPSource);
};

}  // namespace android

#endif  // RTP_SOURCE_H_
