//
// Created by 李振 on 2024/7/25.
//

#include "VEPlayerDriver.h"
#include "VEDef.h"
namespace VE {
    namespace {
        // 同步 prepare() 的最长等待时间，超时视为底层异常
        const std::chrono::seconds kPrepareTimeout(10);
    }

    VEPlayerDriver::VEPlayerDriver()
            : currentState(MEDIA_PLAYER_IDLE), mPlayer(std::make_shared<VEPlayer>()) {

        mPlayerLooper = std::make_shared<ALooper>();
        mPlayerLooper->setName("player_thread");
        mPlayerLooper->start(false);
        mPlayerLooper->registerHandler(mPlayer);

        mPlayer->setOnInfoListener([this](int code, double arg1, std::string str1, void *obj3) {
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_INFO, code, arg1, obj3);
        });

        mPlayer->setOnProgressListener([this](double progress) {
            ALOGD("setOnProgressListener progress:%f", progress);
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS, 0, progress, nullptr);
        });

        // 设置出错回调
        mPlayer->setOnErrorListener([this](int msg1, std::string msg2) {
            // 遇到错误直接切换到 MEDIA_PLAYER_STATE_ERROR
            {
                std::lock_guard<std::mutex> lk(mMutex);
                currentState = MEDIA_PLAYER_STATE_ERROR;
            }
            // prepare() 可能正阻塞等待，错误态也要唤醒它，否则调用线程永久挂起
            mCond.notify_all();
            ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_ERROR enter!!!");
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_ERROR, msg1, 0, &msg2);
            return true;
        });

        // 设置播放结束回调。循环播放由 VEPlayer 内部处理(开启循环时不会回调到这里)，
        // 这里收到就意味着真的播完了。
        mPlayer->setOnCompletionListener([this]() {
            ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION enter!!!");
            {
                std::lock_guard<std::mutex> lk(mMutex);
                currentState = MEDIA_PLAYER_PLAYBACK_COMPLETE;
            }
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION, 0, 0, nullptr);
        });

        mPlayer->setOnPreparedListener([this]() {
            {
                std::lock_guard<std::mutex> lk(mMutex);
                currentState = MEDIA_PLAYER_PREPARED;
            }
            mCond.notify_all();
            ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_PREPARED enter!!!");
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_PREPARED, 0, 0, nullptr);
        });

        mPlayer->setOnSeekComplateListener([this]() {
            {
                std::lock_guard<std::mutex> lk(mMutex);
                mIsSeeking = false;
            }
            // 播放/暂停状态的恢复由 VEPlayer 的 seek 流程内部完成，
            // 这里再插手会和它的分阶段推进打架
            ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE enter!!!");
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE, 0, 0, nullptr);
        });

        mPlayer->setOnEOSListener([this]() {
            std::lock_guard<std::mutex> lk(mMutex);
            ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_EOS enter!!!");
            mIsSeeking = false;
        });
    }

    VEPlayerDriver::~VEPlayerDriver() {
        ALOGI("VEPlayerDriver::%s enter", __FUNCTION__);
        // 顺序很重要：先同步释放播放器(内部会停掉并 join 所有组件线程)，
        // 再停掉 player looper。反过来做的话组件线程还在跑就把对象销毁了。
        if (mPlayer) {
            mPlayer->release();
        }
        if (mPlayerLooper) {
            if (mPlayer) {
                mPlayerLooper->unregisterHandler(mPlayer->id());
            }
            mPlayerLooper->stop();
        }
        mPlayer.reset();
        mPlayerLooper.reset();
        mListener.reset();
        ALOGI("VEPlayerDriver::%s exit", __FUNCTION__);
    }

    VEResult VEPlayerDriver::setDataSource(std::string path) {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_IDLE) {
            return -1;
        }
        if (mPlayer->setDataSource(path) == 0) {
            currentState = MEDIA_PLAYER_INITIALIZED;
            return 0;
        }
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::setSurface(ANativeWindow *win, int width, int height) {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mPlayer->setDisplayOut(win, width, height) == 0) {
            return 0;
        }
        return -1;
    }

    VEResult VEPlayerDriver::prepare() {
        std::unique_lock<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_STOPPED && currentState != MEDIA_PLAYER_INITIALIZED) {
            ALOGD("VEPlayerDriver::%s player status not correct!!!", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        currentState = MEDIA_PLAYER_PREPARING;
        mPlayer->prepare();

        // 带谓词等待，避免 prepare 在 wait 之前完成导致的唤醒丢失；
        // 加超时兜底，底层卡住时不至于把调用线程永久挂起。
        bool done = mCond.wait_for(lk, kPrepareTimeout, [this] {
            return currentState == MEDIA_PLAYER_PREPARED ||
                   currentState == MEDIA_PLAYER_STATE_ERROR;
        });

        if (!done) {
            ALOGE("VEPlayerDriver::%s prepare timed out", __FUNCTION__);
            currentState = MEDIA_PLAYER_STATE_ERROR;
            return VE_TIMED_OUT;
        }

        return currentState == MEDIA_PLAYER_PREPARED ? VE_OK : VE_UNKNOWN_ERROR;
    }

    VEResult VEPlayerDriver::prepareAsync() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_STOPPED && currentState != MEDIA_PLAYER_INITIALIZED) {
            ALOGE("Invalid state for prepareAsync: %d", currentState);
            return -1;
        }
        if (mPlayer->prepareAsync() == 0) {
            currentState = MEDIA_PLAYER_PREPARING;
            return 0;
        }
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::start() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_PAUSED &&
            currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            return -1;
        }
        if (mPlayer->start() == 0) {
            currentState = MEDIA_PLAYER_STARTED;
            return 0;
        }
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::stop() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED &&
            currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            return -1;
        }
        if (mPlayer->stop() == 0) {
            currentState = MEDIA_PLAYER_STOPPED;
            return 0;
        }
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::pause() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_STARTED) {
            return -1;
        }
        if (mPlayer->pause() == 0) {
            currentState = MEDIA_PLAYER_PAUSED;
            return 0;
        }
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::reset() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mPlayer->reset() == 0) {
            currentState = MEDIA_PLAYER_IDLE;
            return 0;
        }
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    int64_t VEPlayerDriver::getDuration() {
        std::lock_guard<std::mutex> lk(mMutex);
        return mPlayer->getDuration();
    }

    int64_t VEPlayerDriver::getCurrentPosition() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState == MEDIA_PLAYER_IDLE || currentState == MEDIA_PLAYER_INITIALIZED ||
            currentState == MEDIA_PLAYER_STATE_ERROR) {
            return 0;
        }
        return mPlayer->getCurrentPosition();
    }

    VEResult VEPlayerDriver::setLooping(bool looping) {
        std::lock_guard<std::mutex> lk(mMutex);
        mEnableLooping = looping;
        mPlayer->setLooping(looping);
        return VE_OK;
    }

    VEResult VEPlayerDriver::setSpeedRate(float speed) {
        std::lock_guard<std::mutex> lk(mMutex);
        // 如实返回底层结果，不要吞掉"未支持"
        return mPlayer->setPlaySpeed(speed);
    }

    VEResult VEPlayerDriver::setListener(std::shared_ptr<MediaPlayerListener> listener) {
        std::lock_guard<std::mutex> lk(mMutex);
        mListener = listener;
        return 0;
    }

    VEResult VEPlayerDriver::seekTo(double timestampMs) {
        std::lock_guard<std::mutex> lk(mMutex);
        ALOGI("VEPlayerDriver::%s timestampMs:%f, currentState:%d", __FUNCTION__, timestampMs,
              currentState);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED &&
            currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            ALOGE("VEPlayerDriver::%s Invalid state for seekTo: %d", __FUNCTION__, currentState);
            return -1;
        }

        // 不在这里丢弃重复 seek：VEPlayer 会合并进行中的 seek 请求，
        // 拖动进度条时按最后一次目标定位，比直接丢弃体验更好
        mIsSeeking = true;

        ALOGI("VEPlayerDriver::%s timestampMs:%f exe seek", __FUNCTION__, timestampMs);
        VEResult result = mPlayer->seek(timestampMs);
        if (result != VE_OK) {
            currentState = MEDIA_PLAYER_STATE_ERROR;
        }
        return result;
    }

    void VEPlayerDriver::notifyListener(int msg, int ext1, double ext2, const void *obj) {
        if (mListener) {
            mListener->notify(msg, ext1, ext2, obj);
        }
    }
}