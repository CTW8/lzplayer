/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef GENERIC_SOURCE_H_

#define GENERIC_SOURCE_H_
#include <unordered_map>
#include <list>
#include <memory>
#include "NuPlayer.h"
#include "NuPlayerSource.h"
#include "MediaBufferBase.h"
#include "MediaClock.h"

namespace android {

struct NuPlayer::GenericSource : public NuPlayer::Source,
                                 public MediaBufferObserver // Modular DRM
{
    GenericSource(const std::shared_ptr<AMessage> &notify, bool uidValid, uid_t uid,
                  const std::shared_ptr<MediaClock> &mediaClock);

    status_t setDataSource(
            const std::shared_ptr<IMediaHTTPService> &httpService,
            const char *url,
            const std::unordered_map<std::string, std::string> *headers);

    status_t setDataSource(int fd, int64_t offset, int64_t length);

    status_t setDataSource(const std::shared_ptr<DataSource>& dataSource);

    virtual status_t getBufferingSettings(
            BufferingSettings* buffering /* nonnull */) override;
    virtual status_t setBufferingSettings(const BufferingSettings& buffering) override;

    virtual void prepareAsync();

    virtual void start();
    virtual void stop();
    virtual void pause();
    virtual void resume();

    virtual void disconnect();

    virtual status_t feedMoreTSData();

    virtual std::shared_ptr<MetaData> getFileFormatMeta() const;

    virtual status_t dequeueAccessUnit(bool audio, std::shared_ptr<ABuffer> *accessUnit);

    virtual status_t getDuration(int64_t *durationUs);
    virtual size_t getTrackCount() const;
    virtual std::shared_ptr<AMessage> getTrackInfo(size_t trackIndex) const;
    virtual ssize_t getSelectedTrack(media_track_type type) const;
    virtual status_t selectTrack(size_t trackIndex, bool select, int64_t timeUs);
    virtual status_t seekTo(
        int64_t seekTimeUs,
        MediaPlayerSeekMode mode = MediaPlayerSeekMode::SEEK_PREVIOUS_SYNC) override;

    virtual bool isStreaming() const;

    // Modular DRM
    virtual void signalBufferReturned(MediaBufferBase *buffer);

    virtual status_t prepareDrm(
            const uint8_t uuid[16], const std::vector<uint8_t> &drmSessionId, std::shared_ptr<ICrypto> *outCrypto);

    virtual status_t releaseDrm();


protected:
    virtual ~GenericSource();

    virtual void onMessageReceived(const std::shared_ptr<AMessage> &msg);

    virtual std::shared_ptr<MetaData> getFormatMeta(bool audio);

private:
    enum {
        kWhatPrepareAsync,
        kWhatFetchSubtitleData,
        kWhatFetchTimedTextData,
        kWhatSendSubtitleData,
        kWhatSendGlobalTimedTextData,
        kWhatSendTimedTextData,
        kWhatChangeAVSource,
        kWhatPollBuffering,
        kWhatSeek,
        kWhatReadBuffer,
        kWhatStart,
        kWhatResume,
        kWhatSecureDecodersInstantiated,
    };

    struct Track {
        size_t mIndex;
        std::shared_ptr<IMediaSource> mSource;
        std::shared_ptr<AnotherPacketSource> mPackets;
    };

    std::vector<std::shared_ptr<IMediaSource> > mSources;
    Track mAudioTrack;
    int64_t mAudioTimeUs;
    int64_t mAudioLastDequeueTimeUs;
    Track mVideoTrack;
    int64_t mVideoTimeUs;
    int64_t mVideoLastDequeueTimeUs;
    Track mSubtitleTrack;
    Track mTimedTextTrack;

    BufferingSettings mBufferingSettings;
    int32_t mPrevBufferPercentage;
    int32_t mPollBufferingGeneration;
    bool mSentPauseOnBuffering;

    int32_t mAudioDataGeneration;
    int32_t mVideoDataGeneration;
    int32_t mFetchSubtitleDataGeneration;
    int32_t mFetchTimedTextDataGeneration;
    int64_t mDurationUs;
    bool mAudioIsVorbis;
    // Secure codec is required.
    bool mIsSecure;
    bool mIsStreaming;
    bool mUIDValid;
    uid_t mUID;
    const std::shared_ptr<MediaClock> mMediaClock;
    std::shared_ptr<IMediaHTTPService> mHTTPService;
    std::string mUri;
    std::unordered_map<std::string, std::string> mUriHeaders;
    base::unique_fd mFd;
    int64_t mOffset;
    int64_t mLength;

    bool mDisconnected;
    std::shared_ptr<DataSource> mDataSource;
    std::shared_ptr<NuCachedSource2> mCachedSource;
    std::shared_ptr<DataSource> mHttpSource;
    std::shared_ptr<MetaData> mFileMeta;
    bool mStarted;
    bool mPreparing;
    int64_t mBitrate;
    uint32_t mPendingReadBufferTypes;
    std::shared_ptr<ABuffer> mGlobalTimedText;

    mutable std::mutex mLock;
    mutable std::mutex mDisconnectLock; // Protects mDataSource, mHttpSource and mDisconnected

    std::shared_ptr<ALooper> mLooper;

    void resetDataSource();

    status_t initFromDataSource();
    int64_t getLastReadPosition();

    void notifyPreparedAndCleanup(status_t err);
    void onSecureDecodersInstantiated(status_t err);
    void finishPrepareAsync();
    status_t startSources();

    void onSeek(const std::shared_ptr<AMessage>& msg);
    status_t doSeek(int64_t seekTimeUs, MediaPlayerSeekMode mode);

    void onPrepareAsync();

    void fetchTextData(
            uint32_t what, media_track_type type,
            int32_t curGen, const std::shared_ptr<AnotherPacketSource>& packets, const std::shared_ptr<AMessage>& msg);

    void sendGlobalTextData(
            uint32_t what,
            int32_t curGen, std::shared_ptr<AMessage> msg);

    void sendTextData(
            uint32_t what, media_track_type type,
            int32_t curGen, const std::shared_ptr<AnotherPacketSource>& packets, const std::shared_ptr<AMessage>& msg);

    std::shared_ptr<ABuffer> mediaBufferToABuffer(
            MediaBufferBase *mbuf,
            media_track_type trackType);

    void postReadBuffer(media_track_type trackType);
    void onReadBuffer(const std::shared_ptr<AMessage>& msg);
    // When |mode| is MediaPlayerSeekMode::SEEK_CLOSEST, the buffer read shall
    // include an item indicating skipping rendering all buffers with timestamp
    // earlier than |seekTimeUs|.
    // For other modes, the buffer read will not include the item as above in order
    // to facilitate fast seek operation.
    void readBuffer(
            media_track_type trackType,
            int64_t seekTimeUs = -1ll,
            MediaPlayerSeekMode mode = MediaPlayerSeekMode::SEEK_PREVIOUS_SYNC,
            int64_t *actualTimeUs = NULL, bool formatChange = false);

    void queueDiscontinuityIfNeeded(
            bool seeking, bool formatChange, media_track_type trackType, Track *track);

    void schedulePollBuffering();
    void onPollBuffering();
    void notifyBufferingUpdate(int32_t percentage);

    void sendCacheStats();

    std::shared_ptr<MetaData> getFormatMeta_l(bool audio);
    int32_t getDataGeneration(media_track_type type) const;

    // Modular DRM
    // The source is DRM protected and is prepared for DRM.
    bool mIsDrmProtected;
    // releaseDrm has been processed.
    bool mIsDrmReleased;
    std::vector<std::string> mMimes;

    status_t checkDrmInfo();

    DISALLOW_EVIL_CONSTRUCTORS(GenericSource);
};

}  // namespace android

#endif  // GENERIC_SOURCE_H_
