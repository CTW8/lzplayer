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

#pragma once

#include <thread>
#include <mutex>
#include <cassert>
#include <condition_variable>
#include <list>

#define DISALLOW_EVIL_CONSTRUCTORS(name) \
    name(const name &); \
    name &operator=(const name &) /* NOLINT */

namespace android {

struct AHandler;
struct AMessage;
struct AReplyToken;

struct ALooper:public std::enable_shared_from_this<ALooper>{
    typedef int32_t event_id;
    typedef int32_t handler_id;

    ALooper();

    // Takes effect in a subsequent call to start().
    void setName(const char *name);

    handler_id registerHandler(const std::shared_ptr<AHandler> &handler);
    void unregisterHandler(handler_id handlerID);

    int32_t start(bool runOnCallingThread);

    int32_t stop();

    static int64_t GetNowUs();

    const char *getName() const {
        return mName.c_str();
    }

    virtual ~ALooper();

private:
    friend struct AMessage;       // post()

    struct Event {
        int64_t mWhenUs;
        std::shared_ptr<AMessage> mMessage;
    };

    std::mutex mLock;
    std::condition_variable mQueueChangedCondition;

    std::string mName;

    std::list<Event> mEventQueue;

    struct LooperThread;
    std::shared_ptr<LooperThread> mThread;
    bool mRunningLocally;

    // use a separate lock for reply handling, as it is always on another thread
    // use a central lock, however, to avoid creating a mutex for each reply
    std::mutex mRepliesLock;
    std::condition_variable mRepliesCondition;

    // START --- methods used only by AMessage

    // posts a message on this looper with the given timeout
    void post(const std::shared_ptr<AMessage> &msg, int64_t delayUs);

    // creates a reply token to be used with this looper
    std::shared_ptr<AReplyToken> createReplyToken();
    // waits for a response for the reply token.  If status is OK, the response
    // is stored into the supplied variable.  Otherwise, it is unchanged.
    int32_t awaitResponse(const std::shared_ptr<AReplyToken> &replyToken, std::shared_ptr<AMessage> *response);
    // posts a reply for a reply token.  If the reply could be successfully posted,
    // it returns OK. Otherwise, it returns an error value.
    int32_t postReply(const std::shared_ptr<AReplyToken> &replyToken, const std::shared_ptr<AMessage> &msg);

    // END --- methods used only by AMessage

    bool loop();

    DISALLOW_EVIL_CONSTRUCTORS(ALooper);
};

} // namespace android
