#ifndef __VEPLAYER__
#define __VEPLAYER__

#include <string>
#include <functional>
#include <atomic>
#include <android/native_window_jni.h>
#include "IVEComponent.h"
#include "VEDemux.h"
#include "VEAudioDecoder.h"
#include "VEVideoDecoder.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEPacketQueue.h"
#include "VEFrameQueue.h"
#include "VEError.h"
#include "jni.h"
#include "thread/AHandler.h"
#include "VEVideoRender.h"
#include "AudioOpenSLESOutput.h"
#include "VEDef.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"


typedef std::function<void(int code,double arg1,std::string str1,void *obj3)> funOnInfoCallback;
typedef std::function<void(int code,std::string msg)> funOnErrorCallback;
typedef std::function<void(void)> funOnCompletionCallback;
typedef std::function<void(double progress)> funOnProgressCallback;
typedef std::function<void()> funOnEOSCallback;
typedef std::function<void()> funOnPreparedCallback;
typedef std::function<void()> funOnSeekComplateCallback;

namespace VE {
    class VEPlayer : public AHandler {
    public:
        VEPlayer();

        ~VEPlayer();

    public:
        /// setDataSource
        VEResult setDataSource(std::string path);

        VEResult setDisplayOut(ANativeWindow *win, int viewWidth, int viewHeight);

        /// prepare
        VEResult prepare();

        VEResult prepareAsync();

        /// start
        VEResult start();

        /// stop
        VEResult stop();

        /// pause
        VEResult pause();

        /// release
        VEResult release();

        /// seekTo
        VEResult seek(double timestampMs);

        /// reset
        VEResult reset();

        void setLooping(bool enable);

        long getCurrentPosition();

        long getDuration();

        void setVolume(int volume);

        VEResult setPlaySpeed(float speed);

        /// setPlaybackParams

        void setOnInfoListener(funOnInfoCallback callback);

        void setOnProgressListener(funOnProgressCallback callback);

        void setOnErrorListener(funOnErrorCallback callback);

        void setOnCompletionListener(funOnCompletionCallback callback);

        void setOnEOSListener(funOnEOSCallback callback);

        void setOnPreparedListener(funOnPreparedCallback callback);

        void setOnSeekComplateListener(funOnSeekComplateCallback callback);

        void notifyInfo(int type, int msg1, double msg2, std::string msg3, void *msg4) {
            if (onInfoCallback) {
                onInfoCallback(msg1, msg2, msg3, msg4);
            }
        }

        void notifyProgress(int64_t progress) {
            if (onProgressCallback) {
                onProgressCallback((double) progress * 1000.f / AV_TIME_BASE);
            }
        }

        void notifyError(int code, const std::string &msg) {
            ALOGE("VEPlayer error code:%d msg:%s", code, msg.c_str());
            if (onErrorCallback) {
                onErrorCallback(code, msg);
            }
        }

    private:

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        void onEOS();

        /// 按固定顺序遍历已创建的组件，缺失的链路自动跳过
        void forEachComponent(const std::function<void(const std::shared_ptr<IVEComponent> &)> &fn);

        /// 按 停数据流 → 释放资源 → 停线程 → 丢对象 的顺序拆掉整条管线。
        /// 前两步靠组件回执握手完成(资源必须在各自线程上释放)，
        /// 因此整个过程是异步的，结束后回调 onDone。
        void teardownComponents(std::function<void()> onDone);

        /// 回执齐了之后的收尾：停 looper、丢对象
        void finishTeardown();

        /// setDataSource 的实际建链部分，拆解完旧管线后才执行
        VEResult setupDataSource(const std::string &path);

    private:
        enum {
            kWhatSetDataSource = '=DaS',
            kWhatPrepare = 'prep',
            kWhatSetVideoSurface = '=VSu',
            kWhatStart = 'strt',
            kWhatVideoNotify = 'vidN',
            kWhatAudioNotify = 'audN',
            kWhatClosedCaptionNotify = 'capN',
            kWhatRendererNotify = 'renN',
            kWhatReset = 'rset',
            kWhatSeek = 'seek',
            kWhatPause = 'paus',
            kWhatStop = 'stop',
            kWhatComponentEvent = 'renE',
            kWhatRelease = 'rele',
            kWhatAckTimeout = 'ackT',
            kWhatProgressTick = 'prgT'
        };

        /// 播放器内部状态。与 VEPlayerDriver 的状态机职责不同：
        /// Driver 负责校验 Java 层 API 调用是否合法，这里负责编排内部流程。
        enum PlayerState {
            STATE_IDLE,
            STATE_PREPARED,
            STATE_STARTED,
            STATE_PAUSED,
            STATE_SEEKING,
            STATE_COMPLETED,
            STATE_RELEASING,   ///< 正在拆解管线，期间拒绝一切播放控制命令
            STATE_ERROR
        };

        /// seek 的分阶段流程，每一阶段都要等齐相关组件的回执才能进入下一阶段
        enum SeekStage {
            SEEK_STAGE_NONE,
            SEEK_STAGE_PAUSING,   ///< 等各组件停止消费数据
            SEEK_STAGE_SEEKING,   ///< 等 demux 定位 + 解码器 flush 完成
            SEEK_STAGE_PRIMING    ///< 等 seek 后的第一帧上屏
        };

        VEResult onSetDataSource(std::shared_ptr<AMessage> msg);

        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart(std::shared_ptr<AMessage> msg);

        VEResult onStop(std::shared_ptr<AMessage> msg);

        VEResult onPause(std::shared_ptr<AMessage> msg);

        VEResult onSeek(std::shared_ptr<AMessage> msg);

        VEResult onReset(std::shared_ptr<AMessage> msg);

        VEResult onRelease(std::shared_ptr<AMessage> msg);

        VEResult onSurfaceChanged(ANativeWindow *win,int viewWidth,int viewHeight);

        VEResult onComponentEvent(const std::shared_ptr<AMessage> &msg);

        // ---- 回执聚合：把"给 N 个组件发命令"变成"等齐 N 个回执后再继续" ----

        /// 已创建组件的位掩码
        uint32_t activeComponentMask() const;

        /// 等待 mask 中所有组件回报 event，齐了之后执行 next。
        /// timeoutUs<=0 时用默认超时。
        void awaitAcks(uint32_t mask, int32_t event, std::function<void()> next,
                       int64_t timeoutUs = 0);

        /// 收到某个组件的回执，清位；清空后触发后续动作
        void onComponentAck(int32_t type, int32_t event);

        /// 回执超时：不让一个丢失的回执把 seek 永久卡住
        void onAckTimeout(const std::shared_ptr<AMessage> &msg);

        // ---- 进度上报：按固定间隔读时钟，而不是每渲染一帧发一条消息 ----
        void startProgressTick();
        void stopProgressTick();
        void onProgressTick(const std::shared_ptr<AMessage> &msg);

        // ---- seek 流程 ----
        void startSeek(double timestampMs);
        void seekStagePause();
        void seekStageSeek();
        void seekStagePrime();
        void seekFinish();

        pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;
        std::shared_ptr<VEDemux> mDemux = nullptr;
        std::shared_ptr<ALooper> mDemuxLooper = nullptr;
        std::shared_ptr<VEAudioDecoder> mAudioDecoder = nullptr;
        std::shared_ptr<ALooper> mAudioDecodeLooper = nullptr;
        std::shared_ptr<VEVideoDecoder> mVideoDecoder = nullptr;
        std::shared_ptr<ALooper> mVideoDecodeLooper = nullptr;
        std::shared_ptr<VEVideoDisplay> mVideoRender = nullptr;
        std::shared_ptr<ALooper> mVideoRenderLooper = nullptr;
        /// 主时钟由播放器持有：启停/定位是播放流程的一部分，
        /// VEAVsync 只拿它做同步判定，不负责它的生命周期
        std::shared_ptr<VEMediaClock> mMediaClock = nullptr;
        std::shared_ptr<VEAVsync> mAVSync = nullptr;

        std::shared_ptr<AMessage> mRenderNotifyMsg = nullptr;

        std::shared_ptr<VEAudioRender> mAudioOutput = nullptr;
        std::shared_ptr<ALooper> mAudioOutputLooper = nullptr;

        std::shared_ptr<VEPacketQueue> mAPacketQueue = nullptr;

        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;

        std::string mPath;

        bool mVideoEOS = false;
        bool mAudioEOS = false;

        /// setLooping 由调用方线程写入，播放器线程读取
        std::atomic<bool> mEnableLoop{false};

        PlayerState mState = STATE_IDLE;

        // 回执聚合状态
        uint32_t mPendingAcks = 0;
        int32_t mExpectedAckEvent = 0;
        int32_t mAckGeneration = 0;
        std::function<void()> mAckContinuation;

        /// 进度上报定时器的代次，stop/pause 时递增以作废在途的 tick
        int32_t mProgressGeneration = 0;

        // seek 流程状态
        SeekStage mSeekStage = SEEK_STAGE_NONE;
        double mSeekTargetMs = 0;
        PlayerState mStateBeforeSeek = STATE_IDLE;
        bool mHasPendingSeek = false;
        double mPendingSeekMs = 0;

        ANativeWindow *mWindow = nullptr;
        int mViewWidth = 0;
        int mViewHeight = 0;

        funOnProgressCallback onProgressCallback;
        funOnInfoCallback onInfoCallback;
        funOnErrorCallback onErrorCallback;
        funOnCompletionCallback onCompleteCallback;
        funOnEOSCallback onEosCallback;
        funOnPreparedCallback onPreparedCallback;
        funOnSeekComplateCallback onSeekComplateCallback;
    };
}

#endif