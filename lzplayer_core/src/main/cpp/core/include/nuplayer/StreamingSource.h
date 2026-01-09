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

#ifndef STREAMING_SOURCE_H_

#define STREAMING_SOURCE_H_

#include "NuPlayer.h"
#include "NuPlayerSource.h"

namespace android {

struct NuPlayer::StreamingSource : public NuPlayer::Source {
    StreamingSource(
            const std::shared_ptr<AMessage> &notify,
            const std::shared_ptr<IStreamSource> &source);

    virtual status_t getBufferingSettings(
            BufferingSettings* buffering /* nonnull */) override;
    virtual status_t setBufferingSettings(const BufferingSettings& buffering) override;

    virtual void prepareAsync();
    virtual void start();

    virtual status_t feedMoreTSData();

    virtual status_t dequeueAccessUnit(bool audio, std::shared_ptr<ABuffer> *accessUnit);

    virtual bool isRealTime() const;

protected:
    virtual ~StreamingSource();

    virtual void onMessageReceived(const std::shared_ptr<AMessage> &msg);

    virtual std::shared_ptr<AMessage> getFormat(bool audio);

private:
    enum {
        kWhatReadBuffer,
    };
    std::shared_ptr<IStreamSource> mSource;
    status_t mFinalResult;
    std::shared_ptr<NuPlayerStreamListener> mStreamListener;
    std::shared_ptr<ATSParser> mTSParser;

    bool mBuffering;
    std::mutex mBufferingLock;
    std::shared_ptr<ALooper> mLooper;

    void setError(status_t err);
    std::shared_ptr<AnotherPacketSource> getSource(bool audio);
    bool haveSufficientDataOnAllTracks();
    status_t postReadBuffer();
    void onReadBuffer();

    DISALLOW_EVIL_CONSTRUCTORS(StreamingSource);
};

}  // namespace android

#endif  // STREAMING_SOURCE_H_
