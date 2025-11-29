/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef NUPLAYER_DECODER_PASS_THROUGH_H_

#define NUPLAYER_DECODER_PASS_THROUGH_H_

#include "NuPlayer.h"

#include "NuPlayerDecoderBase.h"

namespace android {

struct NuPlayer::DecoderPassThrough : public DecoderBase {
    DecoderPassThrough(const std::shared_ptr<AMessage> &notify,
                       const std::shared_ptr<Source> &source,
                       const std::shared_ptr<Renderer> &renderer);

protected:

    virtual ~DecoderPassThrough();

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
        kWhatBufferConsumed     = 'bufC',
    };

    std::shared_ptr<Source> mSource;
    std::shared_ptr<Renderer> mRenderer;
    int64_t mSkipRenderingUntilMediaTimeUs;

    bool    mReachedEOS;

    // Used by feedDecoderInputData to aggregate small buffers into
    // one large buffer.
    std::shared_ptr<ABuffer> mPendingAudioAccessUnit;
    status_t    mPendingAudioErr;
    std::shared_ptr<ABuffer> mAggregateBuffer;

    // mPendingBuffersToDrain are only for debugging. It can be removed
    // when the power investigation is done.
    size_t  mPendingBuffersToDrain;
    size_t  mCachedBytes;
    AString mComponentName;

    bool isStaleReply(const std::shared_ptr<AMessage> &msg);
    bool isDoneFetching() const;

    status_t dequeueAccessUnit(std::shared_ptr<ABuffer> *accessUnit);
    std::shared_ptr<ABuffer> aggregateBuffer(const std::shared_ptr<ABuffer> &accessUnit);
    status_t fetchInputData(std::shared_ptr<AMessage> &reply);
    void doFlush(bool notifyComplete);

    void onInputBufferFetched(const std::shared_ptr<AMessage> &msg);
    void onBufferConsumed(int32_t size);

    DISALLOW_EVIL_CONSTRUCTORS(DecoderPassThrough);
};

}  // namespace android

#endif  // NUPLAYER_DECODER_PASS_THROUGH_H_
