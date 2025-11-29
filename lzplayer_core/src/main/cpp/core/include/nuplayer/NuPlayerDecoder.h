/*
 * Copyright 2014 The Android Open Source Project
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

#ifndef NUPLAYER_DECODER_H_
#define NUPLAYER_DECODER_H_

#include <memory>
#include "NuPlayer.h"
#include "Errors.h"
#include "ALooper.h"

#include "NuPlayerDecoderBase.h"

namespace android {

class MediaCodecBuffer;

struct NuPlayer::Decoder : public DecoderBase {
    Decoder(const std::shared_ptr<AMessage> &notify,
            const std::shared_ptr<Source> &source,
            pid_t pid,
            uid_t uid,
            const std::shared_ptr<Renderer> &renderer = NULL,
            const std::shared_ptr<Surface> &surface = NULL,
            const std::shared_ptr<CCDecoder> &ccDecoder = NULL);

    virtual std::shared_ptr<AMessage> getStats();

    // sets the output surface of video decoders.
    virtual status_t setVideoSurface(const std::shared_ptr<Surface> &surface);

    virtual status_t releaseCrypto();

protected:
    virtual ~Decoder();

    virtual void onMessageReceived(const std::shared_ptr<AMessage> &msg);

    virtual void onConfigure(const std::shared_ptr<AMessage> &format);
    virtual void onSetParameters(const std::shared_ptr<AMessage> &params);
    virtual void onSetRenderer(const std::shared_ptr<Renderer> &renderer);
    virtual void onResume(bool notifyComplete);
    virtual void onFlush();
    virtual void onShutdown(bool notifyComplete);
    virtual bool doRequestBuffers();

private:
    enum {
        kWhatCodecNotify         = 'cdcN',
        kWhatRenderBuffer        = 'rndr',
        kWhatSetVideoSurface     = 'sSur',
        kWhatAudioOutputFormatChanged = 'aofc',
        kWhatDrmReleaseCrypto    = 'rDrm',
    };

    enum {
        kMaxNumVideoTemporalLayers = 32,
    };

    std::shared_ptr<Surface> mSurface;

    std::shared_ptr<Source> mSource;
    std::shared_ptr<Renderer> mRenderer;
    std::shared_ptr<CCDecoder> mCCDecoder;

    std::shared_ptr<AMessage> mInputFormat;
    std::shared_ptr<AMessage> mOutputFormat;
    std::shared_ptr<MediaCodec> mCodec;
    std::shared_ptr<ALooper> mCodecLooper;

    std::list<std::shared_ptr<AMessage> > mPendingInputMessages;

    std::vector<std::shared_ptr<MediaCodecBuffer> > mInputBuffers;
    std::vector<std::shared_ptr<MediaCodecBuffer> > mOutputBuffers;
    std::vector<std::shared_ptr<ABuffer> > mCSDsForCurrentFormat;
    std::vector<std::shared_ptr<ABuffer> > mCSDsToSubmit;
    std::vector<bool> mInputBufferIsDequeued;
    std::vector<MediaBuffer *> mMediaBuffers;
    std::vector<size_t> mDequeuedInputBuffers;

    const pid_t mPid;
    const uid_t mUid;
    int64_t mSkipRenderingUntilMediaTimeUs;
    int64_t mNumFramesTotal;
    int64_t mNumInputFramesDropped;
    int64_t mNumOutputFramesDropped;
    int32_t mVideoWidth;
    int32_t mVideoHeight;
    bool mIsAudio;
    bool mIsVideoAVC;
    bool mIsSecure;
    bool mIsEncrypted;
    bool mIsEncryptedObservedEarlier;
    bool mFormatChangePending;
    bool mTimeChangePending;
    float mFrameRateTotal;
    float mPlaybackSpeed;
    int32_t mNumVideoTemporalLayerTotal;
    int32_t mNumVideoTemporalLayerAllowed;
    int32_t mCurrentMaxVideoTemporalLayerId;
    float mVideoTemporalLayerAggregateFps[kMaxNumVideoTemporalLayers];

    bool mResumePending;
    AString mComponentName;

    void handleError(int32_t err);
    bool handleAnInputBuffer(size_t index);
    bool handleAnOutputBuffer(
            size_t index,
            size_t offset,
            size_t size,
            int64_t timeUs,
            int32_t flags);
    void handleOutputFormatChange(const std::shared_ptr<AMessage> &format);

    void releaseAndResetMediaBuffers();
    void requestCodecNotification();
    bool isStaleReply(const std::shared_ptr<AMessage> &msg);

    void doFlush(bool notifyComplete);
    status_t fetchInputData(std::shared_ptr<AMessage> &reply);
    bool onInputBufferFetched(const std::shared_ptr<AMessage> &msg);
    void onRenderBuffer(const std::shared_ptr<AMessage> &msg);

    bool supportsSeamlessFormatChange(const std::shared_ptr<AMessage> &to) const;
    bool supportsSeamlessAudioFormatChange(const std::shared_ptr<AMessage> &targetFormat) const;
    void rememberCodecSpecificData(const std::shared_ptr<AMessage> &format);
    bool isDiscontinuityPending() const;
    void finishHandleDiscontinuity(bool flushOnTimeChange);

    void notifyResumeCompleteIfNecessary();

    void onReleaseCrypto(const std::shared_ptr<AMessage>& msg);

    DISALLOW_EVIL_CONSTRUCTORS(Decoder);
};

}  // namespace android

#endif  // NUPLAYER_DECODER_H_
