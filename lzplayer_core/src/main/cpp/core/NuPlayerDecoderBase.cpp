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

//#define LOG_NDEBUG 0
#define LOG_TAG "NuPlayerDecoderBase"
#include <utils/Log.h>
#include <inttypes.h>

#include "NuPlayerDecoderBase.h"

#include "NuPlayerRenderer.h"
#include "AMessage.h"

namespace android {

NuPlayer::DecoderBase::DecoderBase(const std::shared_ptr<AMessage> &notify)
    :  mNotify(notify),
       mBufferGeneration(0),
       mPaused(false),
       mStats(std::make_shared<AMessage>()),
       mRequestInputBuffersPending(false) {
    // Every decoder has its own looper because MediaCodec operations
    // are blocking, but NuPlayer needs asynchronous operations.
    mDecoderLooper = std::make_shared<ALooper>();
    mDecoderLooper->setName("NPDecoder");
    mDecoderLooper->start(false, false, ANDROID_PRIORITY_AUDIO);
}

NuPlayer::DecoderBase::~DecoderBase() {
    stopLooper();
}

static
status_t PostAndAwaitResponse(
        const std::shared_ptr<AMessage> &msg, std::shared_ptr<AMessage> *response) {
    status_t err = msg->postAndAwaitResponse(response);

    if (err != OK) {
        return err;
    }

    if (!(*response)->findInt32("err", &err)) {
        err = OK;
    }

    return err;
}

void NuPlayer::DecoderBase::configure(const std::shared_ptr<AMessage> &format) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatConfigure, this);
    msg->setMessage("format", format);
    msg->post();
}

void NuPlayer::DecoderBase::init() {
    mDecoderLooper->registerHandler(this);
}

void NuPlayer::DecoderBase::stopLooper() {
    mDecoderLooper->unregisterHandler(id());
    mDecoderLooper->stop();
}

void NuPlayer::DecoderBase::setParameters(const std::shared_ptr<AMessage> &params) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetParameters, this);
    msg->setMessage("params", params);
    msg->post();
}

void NuPlayer::DecoderBase::setRenderer(const std::shared_ptr<Renderer> &renderer) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSetRenderer, this);
    msg->setObject("renderer", renderer);
    msg->post();
}

void NuPlayer::DecoderBase::pause() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause, this);

    std::shared_ptr<AMessage> response;
    PostAndAwaitResponse(msg, &response);
}

void NuPlayer::DecoderBase::signalFlush() {
    (std::make_shared<AMessage>(kWhatFlush, this))->post();
}

void NuPlayer::DecoderBase::signalResume(bool notifyComplete) {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatResume, this);
    msg->setInt32("notifyComplete", notifyComplete);
    msg->post();
}

void NuPlayer::DecoderBase::initiateShutdown() {
    (std::make_shared<AMessage>(kWhatShutdown, this))->post();
}

void NuPlayer::DecoderBase::onRequestInputBuffers() {
    if (mRequestInputBuffersPending) {
        return;
    }

    // doRequestBuffers() return true if we should request more data
    if (doRequestBuffers()) {
        mRequestInputBuffersPending = true;

        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRequestInputBuffers, this);
        msg->post(10 * 1000LL);
    }
}

void NuPlayer::DecoderBase::onMessageReceived(const std::shared_ptr<AMessage> &msg) {

    switch (msg->what()) {
        case kWhatConfigure:
        {
            std::shared_ptr<AMessage> format;
            CHECK(msg->findMessage("format", &format));
            onConfigure(format);
            break;
        }

        case kWhatSetParameters:
        {
            std::shared_ptr<AMessage> params;
            CHECK(msg->findMessage("params", &params));
            onSetParameters(params);
            break;
        }

        case kWhatSetRenderer:
        {
            std::shared_ptr<RefBase> obj;
            CHECK(msg->findObject("renderer", &obj));
            onSetRenderer(static_cast<Renderer *>(obj.get()));
            break;
        }

        case kWhatPause:
        {
            std::shared_ptr<AReplyToken> replyID;
            CHECK(msg->senderAwaitsResponse(&replyID));

            mPaused = true;

            (std::make_shared<AMessage>)->postReply(replyID);
            break;
        }

        case kWhatRequestInputBuffers:
        {
            mRequestInputBuffersPending = false;
            onRequestInputBuffers();
            break;
        }

        case kWhatFlush:
        {
            onFlush();
            break;
        }

        case kWhatResume:
        {
            int32_t notifyComplete;
            CHECK(msg->findInt32("notifyComplete", &notifyComplete));

            onResume(notifyComplete);
            break;
        }

        case kWhatShutdown:
        {
            onShutdown(true);
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void NuPlayer::DecoderBase::handleError(int32_t err)
{
    // We cannot immediately release the codec due to buffers still outstanding
    // in the renderer.  We signal to the player the error so it can shutdown/release the
    // decoder after flushing and increment the generation to discard unnecessary messages.

    ++mBufferGeneration;

    std::shared_ptr<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatError);
    notify->setInt32("err", err);
    notify->post();
}

}  // namespace android

