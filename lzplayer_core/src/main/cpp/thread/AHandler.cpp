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
//#define LOG_TAG "AHandler"
#include "Log.h"

#include "AHandler.h"
#include "AMessage.h"


void AHandler::deliverMessage(const std::shared_ptr<AMessage> &msg) {
    onMessageReceived(msg);
    mMessageCounter++;

    // if (mVerboseStats) {
    //     uint32_t what = msg->what();
    //     ssize_t idx = mMessages.indexOfKey(what);
    //     if (idx < 0) {
    //         mMessages.add(what, 1);
    //     } else {
    //         mMessages.editValueAt(idx)++;
    //     }
    // }
}
