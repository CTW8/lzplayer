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

#ifndef A_HANDLER_H_
#define A_HANDLER_H_

#include <memory>
#include <map>
#include <unordered_map>
#include "Errors.h"
#include "ALooper.h"

#define DISALLOW_EVIL_CONSTRUCTORS(name) \
    name(const name &); \
    name &operator=(const name &) /* NOLINT */

struct AMessage;

struct AHandler : public std::enable_shared_from_this<AHandler> {
    AHandler()
        : mID(0),
          mVerboseStats(false),
          mMessageCounter(0) {
    }

    ALooper::handler_id id() const {
        return mID;
    }

    std::shared_ptr<ALooper> looper() const {
        return mLooper.lock();
    }

    std::weak_ptr<ALooper> getLooper() const {
        return mLooper;
    }

    std::weak_ptr<AHandler> getHandler(){
        // allow getting a weak reference to a const handler
        return shared_from_this();
    }

protected:
    virtual void onMessageReceived(const std::shared_ptr<AMessage> &msg) = 0;

private:
    friend struct AMessage;      // deliverMessage()
    friend struct ALooperRoster; // setID()

    ALooper::handler_id mID;
    std::weak_ptr<ALooper> mLooper;

    inline void setID(ALooper::handler_id id, const std::weak_ptr<ALooper> &looper) {
        mID = id;
        mLooper = looper;
    }

    bool mVerboseStats;
    uint64_t mMessageCounter;
    std::unordered_map<uint32_t, uint32_t> mMessages;

    void deliverMessage(const std::shared_ptr<AMessage> &msg);

    DISALLOW_EVIL_CONSTRUCTORS(AHandler);
};

#endif  // A_HANDLER_H_
