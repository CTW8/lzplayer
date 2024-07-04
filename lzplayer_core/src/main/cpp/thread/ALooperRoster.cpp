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
#define LOG_TAG "ALooperRoster"
#include "Log.h"


#include "ALooperRoster.h"

#include "ADebug.h"
#include "AHandler.h"
#include "AMessage.h"

static bool verboseStats = false;

ALooperRoster::ALooperRoster()
    : mNextHandlerID(1) {
}

ALooper::handler_id ALooperRoster::registerHandler(
        const std::shared_ptr<ALooper> &looper, const std::shared_ptr<AHandler> &handler) {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (handler->id() != 0) {
        assert(!"A handler must only be registered once.");
        return INVALID_OPERATION;
    }

    HandlerInfo info;
    info.mLooper = looper;
    info.mHandler = handler;
    ALooper::handler_id handlerID = mNextHandlerID++;
    mHandlers.emplace(handlerID, info);

    handler->setID(handlerID, looper);

    return handlerID;
}

void ALooperRoster::unregisterHandler(ALooper::handler_id handlerID) {
    std::lock_guard<std::mutex> autoLock(mLock);

    auto index = mHandlers.find(handlerID);

    if (index == mHandlers.end()) {
        return;
    }

    const HandlerInfo &info = index->second;

    std::shared_ptr<AHandler> handler = info.mHandler.lock();

    if (handler != NULL) {
        handler->setID(0, std::weak_ptr<ALooper>());
    }

    mHandlers.erase(index);
}

void ALooperRoster::unregisterStaleHandlers() {

    std::vector<ALooper::handler_id> unActiveLoopers;
    std::vector<std::shared_ptr<ALooper>> activeLoopers;
    {
        std::lock_guard<std::mutex> autoLock(mLock);

        // for (size_t i = mHandlers.size(); i > 0;) {
        //     i--;
        //     const HandlerInfo &info = mHandlers.at(i);

        //     std::shared_ptr<ALooper> looper = info.mLooper.lock();
        //     if (looper == NULL) {
        //         ALOGV("Unregistering stale handler %d", mHandlers.keyAt(i));
        //         mHandlers.removeItemsAt(i);
        //     } else {
        //         // At this point 'looper' might be the only std::shared_ptr<> keeping
        //         // the object alive. To prevent it from going out of scope
        //         // and having ~ALooper call this method again recursively
        //         // and then deadlocking because of the Autolock above, add
        //         // it to a Vector which will go out of scope after the lock
        //         // has been released.
        //         activeLoopers.add(looper);
        //     }
        // }

        for(auto iter : mHandlers){
            std::shared_ptr<ALooper> looper = iter.second.mLooper.lock();
            if(looper == nullptr){
                unActiveLoopers.push_back(iter.first);
            }else{
                activeLoopers.push_back(looper);
            }
        }

        if(unActiveLoopers.size() != 0){
            for(auto iter : unActiveLoopers){
                mHandlers.erase(iter);
            }
        }
    }
}

static void makeFourCC(uint32_t fourcc, char *s, size_t bufsz) {
    s[0] = (fourcc >> 24) & 0xff;
    if (s[0]) {
        s[1] = (fourcc >> 16) & 0xff;
        s[2] = (fourcc >> 8) & 0xff;
        s[3] = fourcc & 0xff;
        s[4] = 0;
    } else {
        snprintf(s, bufsz, "%u", fourcc);
    }
}

void ALooperRoster::dump(int fd, const std::vector<std::string>& args) {
    // bool clear = false;
    // bool oldVerbose = verboseStats;
    // for (size_t i = 0; i < args.size(); i++) {
    //     if (args[i] == std::string("-c")) {
    //         clear = true;
    //     } else if (args[i] == std::string("-von")) {
    //         verboseStats = true;
    //     } else if (args[i] == std::string("-voff")) {
    //         verboseStats = false;
    //     }
    // }
    // std::string s;
    // if (verboseStats && !oldVerbose) {
    //     s.append("(verbose stats collection enabled, stats will be cleared)\n");
    // }

    // Mutex::Autolock autoLock(mLock);
    // size_t n = mHandlers.size();
    // s.appendFormat(" %zu registered handlers:\n", n);

    // for (size_t i = 0; i < n; i++) {
    //     s.appendFormat("  %d: ", mHandlers.keyAt(i));
    //     HandlerInfo &info = mHandlers.editValueAt(i);
    //     std::shared_ptr<ALooper> looper = info.mLooper.promote();
    //     if (looper != NULL) {
    //         s.append(looper->getName());
    //         std::shared_ptr<AHandler> handler = info.mHandler.promote();
    //         if (handler != NULL) {
    //             handler->mVerboseStats = verboseStats;
    //             s.appendFormat(": %" PRIu64 " messages processed", handler->mMessageCounter);
    //             if (verboseStats) {
    //                 for (size_t j = 0; j < handler->mMessages.size(); j++) {
    //                     char fourcc[15];
    //                     makeFourCC(handler->mMessages.keyAt(j), fourcc, sizeof(fourcc));
    //                     s.appendFormat("\n    %s: %u",
    //                             fourcc,
    //                             handler->mMessages.valueAt(j));
    //                 }
    //             } else {
    //                 handler->mMessages.clear();
    //             }
    //             if (clear || (verboseStats && !oldVerbose)) {
    //                 handler->mMessageCounter = 0;
    //                 handler->mMessages.clear();
    //             }
    //         } else {
    //             s.append(": <stale handler>");
    //         }
    //     } else {
    //         s.append("<stale>");
    //     }
    //     s.append("\n");
    // }
    // (void)write(fd, s.string(), s.size());
}
