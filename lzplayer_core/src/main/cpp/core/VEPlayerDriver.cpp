//
// Created by 李振 on 2024/7/25.
//

#include "VEPlayerDriver.h"
#include "VEDef.h"
namespace VE {

    namespace {
        /// 状态迁移留痕。22 处 mState/currentState 赋值此前绝大多数不打日志，
        /// 于是状态机出问题时没有任何时序可查。级别用 W：它不是错误，但它是
        /// 排查一切状态机问题的唯一线索，不能被日志配额或 Release 剔除掉。
        inline void logTransit(const char *fn, int from, int to) {
            if (from != to) {
                ALOGW("VEPlayerDriver::%s state %d -> %d", fn, from, to);
            }
        }
    }

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
            // code 里带的是具体的 ON_* 事件号(字幕/缓冲/切轨都走这条通道)，
            // 字符串负载(字幕文本)经 str1 传给 Java
            notifyListener(code, static_cast<int>(arg1), arg1, &str1);
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
                logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
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
                logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_PLAYBACK_COMPLETE);
                currentState = MEDIA_PLAYER_PLAYBACK_COMPLETE;
            }
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION, 0, 0, nullptr);
        });

        mPlayer->setOnPreparedListener([this]() {
            {
                std::lock_guard<std::mutex> lk(mMutex);
                // 仅在等待 prepare 时接受：超时后状态已置 ERROR，
                // 迟到的 onPrepared 不允许把 ERROR 悄悄洗成 PREPARED
                if (currentState != MEDIA_PLAYER_PREPARING) {
                    ALOGW("VEPlayerDriver late onPrepared in state %d, ignored", currentState);
                    return;
                }
                logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_PREPARED);
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
            {
                std::lock_guard<std::mutex> lk(mMutex);
                mIsSeeking = false;
            }
            ALOGD("VEPlayerDriver --> VE_PLAYER_NOTIFY_EVENT_ON_EOS enter!!!");
            // 循环播放时收不到 COMPLETION，EOS 是上层感知"到达流尾"的唯一途径
            notifyListener(VE_PLAYER_NOTIFY_EVENT_ON_EOS, 0, 0, nullptr);
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
            // 命令被状态机拒绝。此前这里一行日志都没有——stop→play 的静默失败
            // 就是这么来的：driver 直接 return -1，Java 侧丢弃返回值，无痕可查
            ALOGW("VEPlayerDriver::%s rejected, currentState=%d", __FUNCTION__,
                  (int) currentState);
            return -1;
        }
        if (mPlayer->setDataSource(path) == 0) {
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_INITIALIZED);
            currentState = MEDIA_PLAYER_INITIALIZED;
            return 0;
        }
        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
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

        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_PREPARING);

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
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
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
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_PREPARING);
            currentState = MEDIA_PLAYER_PREPARING;
            return 0;
        }
        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::start() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_PAUSED &&
            currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            // 命令被状态机拒绝。此前这里一行日志都没有——stop→play 的静默失败
            // 就是这么来的：driver 直接 return -1，Java 侧丢弃返回值，无痕可查
            ALOGW("VEPlayerDriver::%s rejected, currentState=%d", __FUNCTION__,
                  (int) currentState);
            return -1;
        }
        if (mPlayer->start() == 0) {
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STARTED);
            currentState = MEDIA_PLAYER_STARTED;
            return 0;
        }
        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::stop() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED &&
            currentState != MEDIA_PLAYER_PAUSED && currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            // 命令被状态机拒绝。此前这里一行日志都没有——stop→play 的静默失败
            // 就是这么来的：driver 直接 return -1，Java 侧丢弃返回值，无痕可查
            ALOGW("VEPlayerDriver::%s rejected, currentState=%d", __FUNCTION__,
                  (int) currentState);
            return -1;
        }
        if (mPlayer->stop() == 0) {
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STOPPED);
            currentState = MEDIA_PLAYER_STOPPED;
            return 0;
        }
        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::pause() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_STARTED) {
            // 命令被状态机拒绝。此前这里一行日志都没有——stop→play 的静默失败
            // 就是这么来的：driver 直接 return -1，Java 侧丢弃返回值，无痕可查
            ALOGW("VEPlayerDriver::%s rejected, currentState=%d", __FUNCTION__,
                  (int) currentState);
            return -1;
        }
        if (mPlayer->pause() == 0) {
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_PAUSED);
            currentState = MEDIA_PLAYER_PAUSED;
            return 0;
        }
        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
        currentState = MEDIA_PLAYER_STATE_ERROR;
        return -1;
    }

    VEResult VEPlayerDriver::reset() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mPlayer->reset() == 0) {
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_IDLE);
            currentState = MEDIA_PLAYER_IDLE;
            return 0;
        }
        logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
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
            logTransit(__FUNCTION__, (int) currentState, (int) MEDIA_PLAYER_STATE_ERROR);
            currentState = MEDIA_PLAYER_STATE_ERROR;
        }
        return result;
    }

    std::string VEPlayerDriver::getTrackInfo() {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState == MEDIA_PLAYER_IDLE ||
            currentState == MEDIA_PLAYER_INITIALIZED ||
            currentState == MEDIA_PLAYER_STATE_ERROR) {
            return "[]";   // 还没 prepare，轨道信息无从谈起
        }
        return mPlayer->getTrackInfoJson();
    }

    VEResult VEPlayerDriver::selectTrack(int trackIndex) {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED &&
            currentState != MEDIA_PLAYER_PAUSED &&
            currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            ALOGE("VEPlayerDriver::%s invalid state %d", __FUNCTION__, currentState);
            return VE_INVALID_OPERATION;
        }
        return mPlayer->selectTrack(trackIndex);
    }

    VEResult VEPlayerDriver::deselectTrack(int trackIndex) {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState != MEDIA_PLAYER_PREPARED && currentState != MEDIA_PLAYER_STARTED &&
            currentState != MEDIA_PLAYER_PAUSED &&
            currentState != MEDIA_PLAYER_PLAYBACK_COMPLETE) {
            return VE_INVALID_OPERATION;
        }
        return mPlayer->deselectTrack(trackIndex);
    }

    VEResult VEPlayerDriver::addExternalSubtitle(const std::string &path) {
        std::lock_guard<std::mutex> lk(mMutex);
        if (currentState == MEDIA_PLAYER_IDLE ||
            currentState == MEDIA_PLAYER_STATE_ERROR) {
            return VE_INVALID_OPERATION;
        }
        return mPlayer->addExternalSubtitle(path);
    }

    std::string VEPlayerDriver::getStats() {
        // 不加状态校验：诊断读数在任何状态下都应该能取到(IDLE 时也是有效信息)
        std::lock_guard<std::mutex> lk(mMutex);
        return mPlayer->getStatsJson();
    }

    std::string VEPlayerDriver::getSeekTrace() {
        std::lock_guard<std::mutex> lk(mMutex);
        return mPlayer->getSeekTraceJson();
    }

    std::string VEPlayerDriver::getStartupTrace() {
        // 同样不校验状态：启播失败的那次数据最该看，不能因为状态是 ERROR 就拒
        std::lock_guard<std::mutex> lk(mMutex);
        return mPlayer->getStartupTraceJson();
    }

    VEResult VEPlayerDriver::setForceSoftwareDecoder(bool force) {
        std::lock_guard<std::mutex> lk(mMutex);
        mPlayer->setForceSoftwareDecoder(force);
        return VE_OK;
    }

    VEResult VEPlayerDriver::setForceSlesAudio(bool force) {
        std::lock_guard<std::mutex> lk(mMutex);
        mPlayer->setForceSlesAudio(force);
        return VE_OK;
    }

    VEResult VEPlayerDriver::setPreferVulkanRender(bool prefer) {
        std::lock_guard<std::mutex> lk(mMutex);
        mPlayer->setPreferVulkanRender(prefer);
        return VE_OK;
    }

    void VEPlayerDriver::notifyListener(int msg, int ext1, double ext2, const void *obj) {
        if (mListener) {
            mListener->notify(msg, ext1, ext2, obj);
        }
    }
}