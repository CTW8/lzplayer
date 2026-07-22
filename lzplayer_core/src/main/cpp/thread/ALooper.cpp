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
//#define LOG_TAG "ALooper"
#include "ALooper.h"

#include "ALooperRoster.h"
#include "AMessage.h"

ALooperRoster gLooperRoster;

struct ALooper::LooperThread{
    LooperThread(ALooper *looper)
        : mLooper(looper){
            mExitPending = false;
    }

    status_t run(std::string name){
        mThread = std::thread([this,name](){
            pthread_setname_np(pthread_self(), name.c_str());
            threadLoop();
        });

        return OK;
    }

    bool threadLoop() {
        mThreadId = std::this_thread::get_id();
        // 退出与否完全由 loop() 决定：停止时它会先把队列里剩余的消息处理完
        // 再返回 false，这样组件的资源释放消息不会被丢弃。
        while (mLooper->loop()) {
        }
        ALOGD("looper thread exit");
        return true;
    }

    void requestExit(){
        std::lock_guard<std::mutex> lk(mLock);
        mExitPending = true;
    }

    status_t requestExitAndWait(){
        std::lock_guard<std::mutex> lk(mLock);
        mExitPending = true;

        if(mThread.joinable()){
            mThread.join();
        }

        mExitPending = false;
        return 0;
    }

    bool isCurrentThread() const {
        return mThreadId == std::this_thread::get_id();
    }

    ~LooperThread() {
        ALOGD("~LooperThread");
        if(mThread.joinable()){
            mThread.join();
        }
    }

private:
    ALooper *mLooper;
    std::thread mThread;
    std::thread::id mThreadId;
    volatile bool mExitPending;
    std::mutex mLock;
    DISALLOW_EVIL_CONSTRUCTORS(LooperThread);
};

// static
int64_t ALooper::GetNowUs() {
    // 必须用单调时钟：所有延时消息(视频帧按 waitTime 投递)都基于它，
    // system_clock 会被 NTP 校时/用户改时间回拨，导致渲染时序错乱。
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

ALooper::ALooper()
    : mRunningLocally(false) {
    // clean up stale AHandlers. Doing it here instead of in the destructor avoids
    // the side effect of objects being deleted from the unregister function recursively.
    gLooperRoster.unregisterStaleHandlers();
}

ALooper::~ALooper() {
    stop();
    // stale AHandlers are now cleaned up in the constructor of the next ALooper to come along
}

void ALooper::setName(const char *name) {
    mName = name;
}

ALooper::handler_id ALooper::registerHandler(const std::shared_ptr<AHandler> &handler) {
    return gLooperRoster.registerHandler(shared_from_this(), handler);
}

void ALooper::unregisterHandler(handler_id handlerID) {
    gLooperRoster.unregisterHandler(handlerID);
}

status_t ALooper::start(
        bool runOnCallingThread) {
    if (runOnCallingThread) {
        {
            std::lock_guard<std::mutex> autoLock(mLock);

            if (mThread != NULL || mRunningLocally) {
                return INVALID_OPERATION;
            }

            mRunningLocally = true;
        }

        do {
        } while (loop());

        return OK;
    }

    std::lock_guard<std::mutex> autoLock(mLock);

    if (mThread != NULL || mRunningLocally) {
        return INVALID_OPERATION;
    }

    mThread = std::make_shared<LooperThread>(this);
    ALOGE("run thread");
    status_t err = mThread->run(
            mName.empty() ? "ALooper" : mName.c_str());
    if (err != OK) {
        mThread.reset();
    }
    ALOGE("ALooper::start done");
    return err;
}

status_t ALooper::stop() {
    std::shared_ptr<LooperThread> thread;
    bool runningLocally;

    {
        std::lock_guard<std::mutex> autoLock(mLock);

        thread = mThread;
        runningLocally = mRunningLocally;
        mThread.reset();
        mRunningLocally = false;
    }

    if (thread == NULL && !runningLocally) {
        return INVALID_OPERATION;
    }

    if (thread != NULL) {
        thread->requestExit();
    }

    mQueueChangedCondition.notify_all();
    {
        std::lock_guard<std::mutex> autoLock(mRepliesLock);
        mRepliesCondition.notify_all();
    }

    if (!runningLocally && !thread->isCurrentThread()) {
        // If not running locally and this thread _is_ the looper thread,
        // the loop() function will return and never be called again.
        thread->requestExitAndWait();
    }

    return OK;
}

void ALooper::post(const std::shared_ptr<AMessage> &msg, int64_t delayUs) {
    std::lock_guard<std::mutex> autoLock(mLock);
    int64_t whenUs;
    if (delayUs > 0) {
        int64_t nowUs = GetNowUs();
        whenUs = (delayUs > INT64_MAX - nowUs ? INT64_MAX : nowUs + delayUs);

    } else {
        whenUs = GetNowUs();
    }

    std::list<Event>::iterator it = mEventQueue.begin();
    while (it != mEventQueue.end() && (*it).mWhenUs <= whenUs) {
        ++it;
    }

    Event event;
    event.mWhenUs = whenUs;
    event.mMessage = msg;
    if (it == mEventQueue.begin()) {
        mQueueChangedCondition.notify_all();
    }

    mEventQueue.insert(it, event);
//    ALOGD("ALooper::post thread name: %s, event queue size: %zu", mName.c_str(), mEventQueue.size());
}

bool ALooper::loop() {
    Event event;
//    ALOGD("ALooper::loop enter thread name: %s, event queue size: %zu", mName.c_str(), mEventQueue.size());
    {
        std::unique_lock<std::mutex> autoLock(mLock);

        if (mThread == NULL && !mRunningLocally) {
            // 正在停止：把队列排空后再退出。停止流程会先投递 release 之类的
            // 清理消息，直接返回会让这些消息连同资源一起被丢掉。
            // mDrainBudget 防止自我重投的消息导致排空过程不收敛。
            if (mEventQueue.empty() || mDrainBudget-- <= 0) {
                ALOGW("exit loop, %zu message(s) dropped", mEventQueue.size());
                return false;
            }
            // 排空阶段忽略延时，剩余消息立即执行
            event = *mEventQueue.begin();
            mEventQueue.erase(mEventQueue.begin());
        } else {
            if (mEventQueue.empty()) {
                mQueueChangedCondition.wait(autoLock);
                return true;
            }
            int64_t whenUs = (*mEventQueue.begin()).mWhenUs;
            int64_t nowUs = GetNowUs();

            if (whenUs > nowUs) {
                int64_t delayUs = whenUs - nowUs;
                if (delayUs > INT64_MAX / 1000) {
                    delayUs = INT64_MAX / 1000;
                }
                // mQueueChangedCondition.waitRelative(mLock, delayUs * 1000ll);
                mQueueChangedCondition.wait_for(autoLock,std::chrono::nanoseconds(delayUs * 1000ll));

                return true;
            }

            event = *mEventQueue.begin();
            mEventQueue.erase(mEventQueue.begin());
        }

//        ALOGD("ALooper::loop thread name: %s, event queue size: %zu", mName.c_str(), mEventQueue.size());
    }

    event.mMessage->deliver();

    // NOTE: It's important to note that at this point our "ALooper" object
    // may no longer exist (its final reference may have gone away while
    // delivering the message). We have made sure, however, that loop()
    // won't be called again.

    return true;
}

// to be called by AMessage::postAndAwaitResponse only
std::shared_ptr<AReplyToken> ALooper::createReplyToken() {
    return std::make_shared<AReplyToken>(shared_from_this());
}

// to be called by AMessage::postAndAwaitResponse only
status_t ALooper::awaitResponse(const std::shared_ptr<AReplyToken> &replyToken, std::shared_ptr<AMessage> *response) {
    // return status in case we want to handle an interrupted wait
    std::unique_lock<std::mutex> autoLock(mRepliesLock);
    assert(replyToken != NULL);
    while (!replyToken->retrieveReply(response)) {
        {
            std::lock_guard<std::mutex> autoLock(mLock);
            if (mThread == NULL) {
                return -ENOENT;
            }
        }
        mRepliesCondition.wait(autoLock);
    }
    return OK;
}

status_t ALooper::postReply(const std::shared_ptr<AReplyToken> &replyToken, const std::shared_ptr<AMessage> &reply) {
    std::lock_guard<std::mutex> autoLock(mRepliesLock);
    status_t err = replyToken->setReply(reply);
    if (err == OK) {
        mRepliesCondition.notify_all();
    }
    return err;
}
