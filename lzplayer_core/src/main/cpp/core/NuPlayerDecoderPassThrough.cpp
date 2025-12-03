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

//#define LOG_NDEBUG 0
#define LOG_TAG "NuPlayerDecoderPassThrough"
#include <utils/Log.h>
#include <inttypes.h>

#include "NuPlayerDecoderPassThrough.h"

#include "NuPlayerRenderer.h"
#include "NuPlayerSource.h"

namespace android {

// TODO optimize buffer size for power consumption
// The offload read buffer size is 32 KB but 24 KB uses less power.
static const size_t kAggregateBufferSizeBytes = 24 * 1024;
static const size_t kMaxCachedBytes = 200000;

NuPlayer::DecoderPassThrough::DecoderPassThrough(
        const std::shared_ptr<AMessage> &notify,
        const std::shared_ptr<Source> &source,
        const std::shared_ptr<Renderer> &renderer)
    : DecoderBase(notify),
      mSource(source),
      mRenderer(renderer),
      mSkipRenderingUntilMediaTimeUs(-1LL),
      mReachedEOS(true),
      mPendingAudioErr(OK),
      mPendingBuffersToDrain(0),
      mCachedBytes(0),
      mComponentName("pass through decoder") {
    ALOGW_IF(renderer == NULL, "expect a non-NULL renderer");
}

NuPlayer::DecoderPassThrough::~DecoderPassThrough() {
}

void NuPlayer::DecoderPassThrough::onConfigure(const std::shared_ptr<AMessage> &format) {
    ALOGV("[%s] onConfigure", mComponentName.c_str());
    mCachedBytes = 0;
    mPendingBuffersToDrain = 0;
    mReachedEOS = false;
    ++mBufferGeneration;

    onRequestInputBuffers();

    int32_t hasVideo = 0;
    format->findInt32("has-video", &hasVideo);

    // The audio sink is already opened before the PassThrough decoder is created.
    // Opening again might be relevant if decoder is instantiated after shutdown and
    // format is different.
    status_t err = mRenderer->openAudioSink(
            format, true /* offloadOnly */, hasVideo,
            AUDIO_OUTPUT_FLAG_NONE /* flags */, NULL /* isOffloaded */, mSource->isStreaming());
    if (err != OK) {
        handleError(err);
    }
}

void NuPlayer::DecoderPassThrough::onSetParameters(const std::shared_ptr<AMessage> &/*params*/) {
    ALOGW("onSetParameters() called unexpectedly");
}

void NuPlayer::DecoderPassThrough::onSetRenderer(
        const std::shared_ptr<Renderer> &renderer) {
    // renderer can't be changed during offloading
    ALOGW_IF(renderer != mRenderer,
            "ignoring request to change renderer");
}

bool NuPlayer::DecoderPassThrough::isStaleReply(const std::shared_ptr<AMessage> &msg) {
    int32_t generation;
    CHECK(msg->findInt32("generation", &generation));
    return generation != mBufferGeneration;
}

bool NuPlayer::DecoderPassThrough::isDoneFetching() const {
    ALOGV("[%s] mCachedBytes = %zu, mReachedEOS = %d mPaused = %d",
            mComponentName.c_str(), mCachedBytes, mReachedEOS, mPaused);

    return mCachedBytes >= kMaxCachedBytes || mReachedEOS || mPaused;
}

/*
 * returns true if we should request more data
 */
bool NuPlayer::DecoderPassThrough::doRequestBuffers() {
    status_t err = OK;
    while (!isDoneFetching()) {
        std::shared_ptr<AMessage> msg = new AMessage();

        err = fetchInputData(msg);
        if (err != OK) {
            break;
        }

        onInputBufferFetched(msg);
    }

    return err == -EWOULDBLOCK
            && mSource->feedMoreTSData() == OK;
}

status_t NuPlayer::DecoderPassThrough::dequeueAccessUnit(std::shared_ptr<ABuffer> *accessUnit) {
    status_t err;

    // Did we save an accessUnit earlier because of a discontinuity?
    if (mPendingAudioAccessUnit != NULL) {
        *accessUnit = mPendingAudioAccessUnit;
        mPendingAudioAccessUnit.clear();
        err = mPendingAudioErr;
        ALOGV("feedDecoderInputData() use mPendingAudioAccessUnit");
    } else {
        err = mSource->dequeueAccessUnit(true /* audio */, accessUnit);
    }

    if (err == INFO_DISCONTINUITY || err == ERROR_END_OF_STREAM) {
        if (mAggregateBuffer != NULL) {
            // We already have some data so save this for later.
            mPendingAudioErr = err;
            mPendingAudioAccessUnit = *accessUnit;
            (*accessUnit).clear();
            ALOGD("return aggregated buffer and save err(=%d) for later", err);
            err = OK;
        }
    }

    return err;
}

std::shared_ptr<ABuffer> NuPlayer::DecoderPassThrough::aggregateBuffer(
        const std::shared_ptr<ABuffer> &accessUnit) {
    std::shared_ptr<ABuffer> aggregate;

    if (accessUnit == NULL) {
        // accessUnit is saved to mPendingAudioAccessUnit
        // return current mAggregateBuffer
        aggregate = mAggregateBuffer;
        mAggregateBuffer.clear();
        return aggregate;
    }

    size_t smallSize = accessUnit->size();
    if ((mAggregateBuffer == NULL)
            // Don't bother if only room for a few small buffers.
            && (smallSize < (kAggregateBufferSizeBytes / 3))) {
        // Create a larger buffer for combining smaller buffers from the extractor.
        mAggregateBuffer = new ABuffer(kAggregateBufferSizeBytes);
        mAggregateBuffer->setRange(0, 0); // start empty
    }

    if (mAggregateBuffer != NULL) {
        int64_t timeUs;
        int64_t dummy;
        bool smallTimestampValid = accessUnit->meta()->findInt64("timeUs", &timeUs);
        bool bigTimestampValid = mAggregateBuffer->meta()->findInt64("timeUs", &dummy);
        // Will the smaller buffer fit?
        size_t bigSize = mAggregateBuffer->size();
        size_t roomLeft = mAggregateBuffer->capacity() - bigSize;
        // Should we save this small buffer for the next big buffer?
        // If the first small buffer did not have a timestamp then save
        // any buffer that does have a timestamp until the next big buffer.
        if ((smallSize > roomLeft)
            || (!bigTimestampValid && (bigSize > 0) && smallTimestampValid)) {
            mPendingAudioErr = OK;
            mPendingAudioAccessUnit = accessUnit;
            aggregate = mAggregateBuffer;
            mAggregateBuffer.clear();
        } else {
            // Grab time from first small buffer if available.
            if ((bigSize == 0) && smallTimestampValid) {
                mAggregateBuffer->meta()->setInt64("timeUs", timeUs);
            }
            // Append small buffer to the bigger buffer.
            memcpy(mAggregateBuffer->base() + bigSize, accessUnit->data(), smallSize);
            bigSize += smallSize;
            mAggregateBuffer->setRange(0, bigSize);

            ALOGV("feedDecoderInputData() smallSize = %zu, bigSize = %zu, capacity = %zu",
                    smallSize, bigSize, mAggregateBuffer->capacity());
        }
    } else {
        // decided not to aggregate
        aggregate = accessUnit;
    }

    return aggregate;
}

status_t NuPlayer::DecoderPassThrough::fetchInputData(std::shared_ptr<AMessage> &reply) {
    std::shared_ptr<ABuffer> accessUnit;

    do {
        status_t err = dequeueAccessUnit(&accessUnit);

        if (err == -EWOULDBLOCK) {
            // Flush out the aggregate buffer to try to avoid underrun.
            accessUnit = aggregateBuffer(NULL /* accessUnit */);
            if (accessUnit != NULL) {
                break;
            }
            return err;
        } else if (err != OK) {
            if (err == INFO_DISCONTINUITY) {
                int32_t type;
                CHECK(accessUnit->meta()->findInt32("discontinuity", &type));

                bool formatChange =
                        (type & ATSParser::DISCONTINUITY_AUDIO_FORMAT) != 0;

                bool timeChange =
                        (type & ATSParser::DISCONTINUITY_TIME) != 0;

                ALOGI("audio discontinuity (formatChange=%d, time=%d)",
                        formatChange, timeChange);

                if (formatChange || timeChange) {
                    std::shared_ptr<AMessage> msg = mNotify->dup();
                    msg->setInt32("what", kWhatInputDiscontinuity);
                    // will perform seamless format change,
                    // only notify NuPlayer to scan sources
                    msg->setInt32("formatChange", false);
                    msg->post();
                }

                if (timeChange) {
                    doFlush(false /* notifyComplete */);
                    err = OK;
                } else if (formatChange) {
                    // do seamless format change
                    err = OK;
                } else {
                    // This stream is unaffected by the discontinuity
                    return -EWOULDBLOCK;
                }
            }

            reply->setInt32("err", err);
            return OK;
        }

        accessUnit = aggregateBuffer(accessUnit);
    } while (accessUnit == NULL);

#if 0
    int64_t mediaTimeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &mediaTimeUs));
    ALOGV("feeding audio input buffer at media time %.2f secs",
         mediaTimeUs / 1E6);
#endif

    reply->setBuffer("buffer", accessUnit);

    return OK;
}

void NuPlayer::DecoderPassThrough::onInputBufferFetched(
        const std::shared_ptr<AMessage> &msg) {
    if (mReachedEOS) {
        return;
    }

    std::shared_ptr<ABuffer> buffer;
    bool hasBuffer = msg->findBuffer("buffer", &buffer);
    if (buffer == NULL) {
        int32_t streamErr = ERROR_END_OF_STREAM;
        CHECK(msg->findInt32("err", &streamErr) || !hasBuffer);
        if (streamErr == OK) {
            return;
        }

        if (streamErr != ERROR_END_OF_STREAM) {
            handleError(streamErr);
        }
        mReachedEOS = true;
        if (mRenderer != NULL) {
            mRenderer->queueEOS(true /* audio */, ERROR_END_OF_STREAM);
        }
        return;
    }

    std::shared_ptr<AMessage> extra;
    if (buffer->meta()->findMessage("extra", &extra) && extra != NULL) {
        int64_t resumeAtMediaTimeUs;
        if (extra->findInt64(
                    "resume-at-mediatimeUs", &resumeAtMediaTimeUs)) {
            ALOGI("[%s] suppressing rendering until %lld us",
                    mComponentName.c_str(), (long long)resumeAtMediaTimeUs);
            mSkipRenderingUntilMediaTimeUs = resumeAtMediaTimeUs;
        }
    }

    int32_t bufferSize = buffer->size();
    mCachedBytes += bufferSize;

    int64_t timeUs = 0;
    CHECK(buffer->meta()->findInt64("timeUs", &timeUs));
    if (mSkipRenderingUntilMediaTimeUs >= 0) {
        if (timeUs < mSkipRenderingUntilMediaTimeUs) {
            ALOGV("[%s] dropping buffer at time %lld as requested.",
                     mComponentName.c_str(), (long long)timeUs);

            onBufferConsumed(bufferSize);
            return;
        }

        mSkipRenderingUntilMediaTimeUs = -1;
    }

    if (mRenderer == NULL) {
        onBufferConsumed(bufferSize);
        return;
    }

    std::shared_ptr<AMessage> reply = std::make_shared<AMessage>(kWhatBufferConsumed, this);
    reply->setInt32("generation", mBufferGeneration);
    reply->setInt32("size", bufferSize);

    std::shared_ptr<MediaCodecBuffer> mcBuffer = std::make_shared<MediaCodecBuffer>(nullptr, buffer);
    mcBuffer->meta()->setInt64("timeUs", timeUs);

    mRenderer->queueBuffer(true /* audio */, mcBuffer, reply);

    ++mPendingBuffersToDrain;
    ALOGV("onInputBufferFilled: #ToDrain = %zu, cachedBytes = %zu",
            mPendingBuffersToDrain, mCachedBytes);
}

void NuPlayer::DecoderPassThrough::onBufferConsumed(int32_t size) {
    --mPendingBuffersToDrain;
    mCachedBytes -= size;
    ALOGV("onBufferConsumed: #ToDrain = %zu, cachedBytes = %zu",
            mPendingBuffersToDrain, mCachedBytes);
    onRequestInputBuffers();
}

void NuPlayer::DecoderPassThrough::onResume(bool notifyComplete) {
    mPaused = false;

    onRequestInputBuffers();

    if (notifyComplete) {
        std::shared_ptr<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatResumeCompleted);
        notify->post();
    }
}

void NuPlayer::DecoderPassThrough::doFlush(bool notifyComplete) {
    ++mBufferGeneration;
    mSkipRenderingUntilMediaTimeUs = -1;
    mPendingAudioAccessUnit.clear();
    mPendingAudioErr = OK;
    mAggregateBuffer.clear();

    if (mRenderer != NULL) {
        mRenderer->flush(true /* audio */, notifyComplete);
        mRenderer->signalTimeDiscontinuity();
    }

    mPendingBuffersToDrain = 0;
    mCachedBytes = 0;
    mReachedEOS = false;
}

void NuPlayer::DecoderPassThrough::onFlush() {
    doFlush(true /* notifyComplete */);

    mPaused = true;
    std::shared_ptr<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushCompleted);
    notify->post();

}

void NuPlayer::DecoderPassThrough::onShutdown(bool notifyComplete) {
    ++mBufferGeneration;
    mSkipRenderingUntilMediaTimeUs = -1;

    if (notifyComplete) {
        std::shared_ptr<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatShutdownCompleted);
        notify->post();
    }

    mReachedEOS = true;
}

void NuPlayer::DecoderPassThrough::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    ALOGV("[%s] onMessage: %s", mComponentName.c_str(),
            msg->debugString().c_str());

    switch (msg->what()) {
        case kWhatBufferConsumed:
        {
            if (!isStaleReply(msg)) {
                int32_t size;
                CHECK(msg->findInt32("size", &size));
                onBufferConsumed(size);
            }
            break;
        }

        default:
            DecoderBase::onMessageReceived(msg);
            break;
    }
}

}  // namespace android
