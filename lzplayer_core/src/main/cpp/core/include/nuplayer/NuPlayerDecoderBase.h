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

#ifndef NUPLAYER_DECODER_BASE_H_

#define NUPLAYER_DECODER_BASE_H_

#include "NuPlayer.h"

#include "AHandler.h"

namespace android {

struct ABuffer;
struct MediaCodec;
class MediaBuffer;
class MediaCodecBuffer;
class Surface;

struct NuPlayer::DecoderBase : public AHandler {
    explicit DecoderBase(const std::shared_ptr<AMessage> &notify);

    void configure(const std::shared_ptr<AMessage> &format);
    void init();
    void setParameters(const std::shared_ptr<AMessage> &params);

    // Synchronous call to ensure decoder will not request or send out data.
    void pause();

    void setRenderer(const std::shared_ptr<Renderer> &renderer);
    virtual status_t setVideoSurface(const std::shared_ptr<Surface> &) { return INVALID_OPERATION; }

    void signalFlush();
    void signalResume(bool notifyComplete);
    void initiateShutdown();

    virtual std::shared_ptr<AMessage> getStats() {
        return mStats;
    }

    virtual status_t releaseCrypto() {
        return INVALID_OPERATION;
    }

    enum {
        kWhatInputDiscontinuity  = 'inDi',
        kWhatVideoSizeChanged    = 'viSC',
        kWhatFlushCompleted      = 'flsC',
        kWhatShutdownCompleted   = 'shDC',
        kWhatResumeCompleted     = 'resC',
        kWhatEOS                 = 'eos ',
        kWhatError               = 'err ',
    };

protected:

    virtual ~DecoderBase();

    void stopLooper();

    virtual void onMessageReceived(const std::shared_ptr<AMessage> &msg);

    virtual void onConfigure(const std::shared_ptr<AMessage> &format) = 0;
    virtual void onSetParameters(const std::shared_ptr<AMessage> &params) = 0;
    virtual void onSetRenderer(const std::shared_ptr<Renderer> &renderer) = 0;
    virtual void onResume(bool notifyComplete) = 0;
    virtual void onFlush() = 0;
    virtual void onShutdown(bool notifyComplete) = 0;

    void onRequestInputBuffers();
    virtual bool doRequestBuffers() = 0;
    virtual void handleError(int32_t err);

    std::shared_ptr<AMessage> mNotify;
    int32_t mBufferGeneration;
    bool mPaused;
    std::shared_ptr<AMessage> mStats;
    std::mutex mStatsLock;

private:
    enum {
        kWhatConfigure           = 'conf',
        kWhatSetParameters       = 'setP',
        kWhatSetRenderer         = 'setR',
        kWhatPause               = 'paus',
        kWhatRequestInputBuffers = 'reqB',
        kWhatFlush               = 'flus',
        kWhatShutdown            = 'shuD',
    };

    std::shared_ptr<ALooper> mDecoderLooper;
    bool mRequestInputBuffersPending;

    DISALLOW_EVIL_CONSTRUCTORS(DecoderBase);
};

}  // namespace android

#endif  // NUPLAYER_DECODER_BASE_H_
