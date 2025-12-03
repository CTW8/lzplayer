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

#ifndef ANOTHER_PACKET_SOURCE_H_

#define ANOTHER_PACKET_SOURCE_H_
#include <mutex>
#include <list>
#include <unordered_map>
#include <condition_variable>
#include "Errors.h"
#include "AMessage.h"
#include "MetaData.h"
#include "MediaSource.h"

namespace android {

struct ABuffer;

struct AnotherPacketSource : public MediaSource {
    explicit AnotherPacketSource(const std::shared_ptr<MetaData> &meta);

    void setFormat(const std::shared_ptr<MetaData> &meta);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual std::shared_ptr<MetaData> getFormat();

    virtual status_t read(
            MediaBufferBase **buffer, const ReadOptions *options = NULL);

    void clear();

    // Returns true if we have any packets including discontinuities
    bool hasBufferAvailable(status_t *finalResult);

    // Returns true if we have packets that's not discontinuities
    bool hasDataBufferAvailable(status_t *finalResult);

    // Returns the number of available buffers. finalResult is always OK
    // if this method returns non-0, or the final result if it returns 0.
    size_t getAvailableBufferCount(status_t *finalResult);

    // Returns the difference between the last and the first queued
    // presentation timestamps since the last discontinuity (if any).
    int64_t getBufferedDurationUs(status_t *finalResult);

    // Returns the difference between the two largest timestamps queued
    int64_t getEstimatedBufferDurationUs();

    status_t nextBufferTime(int64_t *timeUs);

    void queueAccessUnit(const std::shared_ptr<ABuffer> &buffer);

    void queueDiscontinuity(
            ATSParser::DiscontinuityType type,
            const std::shared_ptr<AMessage> &extra,
            bool discard);

    void signalEOS(status_t result);

    status_t dequeueAccessUnit(std::shared_ptr<ABuffer> *buffer);
    void requeueAccessUnit(const std::shared_ptr<ABuffer> &buffer);

    bool isFinished(int64_t duration) const;

    void enable(bool enable);

    std::shared_ptr<AMessage> getLatestEnqueuedMeta();
    std::shared_ptr<AMessage> getLatestDequeuedMeta();
    std::shared_ptr<AMessage> getMetaAfterLastDequeued(int64_t delayUs);

    void trimBuffersAfterMeta(const std::shared_ptr<AMessage> &meta);
    std::shared_ptr<AMessage> trimBuffersBeforeMeta(const std::shared_ptr<AMessage> &meta);

protected:
    virtual ~AnotherPacketSource();

private:

    struct DiscontinuitySegment {
        int64_t mMaxDequeTimeUs, mMaxEnqueTimeUs;
        DiscontinuitySegment()
            : mMaxDequeTimeUs(-1),
              mMaxEnqueTimeUs(-1) {
        };

        void clear() {
            mMaxDequeTimeUs = mMaxEnqueTimeUs = -1;
        }
    };

    // Discontinuity segments are consecutive access units between
    // discontinuity markers. There should always be at least _ONE_
    // discontinuity segment, hence the various CHECKs in
    // AnotherPacketSource.cpp for non-empty()-ness.
    std::list<DiscontinuitySegment> mDiscontinuitySegments;

    std::mutex mLock;
    std::condition_variable mCondition;

    bool mIsAudio;
    bool mIsVideo;
    bool mEnabled;
    std::shared_ptr<MetaData> mFormat;
    int64_t mLastQueuedTimeUs;
    int64_t mEstimatedBufferDurationUs;
    std::list<std::shared_ptr<ABuffer> > mBuffers;
    status_t mEOSResult;
    std::shared_ptr<AMessage> mLatestEnqueuedMeta;
    std::shared_ptr<AMessage> mLatestDequeuedMeta;

    bool wasFormatChange(int32_t discontinuityType) const;

    DISALLOW_EVIL_CONSTRUCTORS(AnotherPacketSource);
};


}  // namespace android

#endif  // ANOTHER_PACKET_SOURCE_H_
