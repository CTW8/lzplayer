#ifndef __VEPLAYER__
#define __VEPLAYER__

#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <android/native_window_jni.h>
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

        /// 按角色的组件状态机(仿 NuPlayer mFlushingAudio/mFlushingVideo)。
        /// 回执只有在对应的 *ING 态才被接受——这就是防过期/重复回执的守卫：
        /// fire-and-forget 命令产生的回执、被中止流程的迟到回执，到达时
        /// 角色不在等待态，直接丢弃。
        enum RoleState {
            ROLE_NONE,            ///< 该链路不存在(纯音频/纯视频)
            ROLE_ACTIVE,
            ROLE_PAUSING,   ROLE_PAUSED,     ///< seek 阶段①
            ROLE_SEEKING,   ROLE_SEEK_DONE,  ///< seek 阶段②
            ROLE_STOPPING,  ROLE_STOPPED,    ///< teardown 阶段①
            ROLE_RELEASING, ROLE_RELEASED    ///< teardown 阶段②
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

        // ---- 角色状态机：每个组件一个状态变量，回执按角色守卫接收 ----

        /// EComponentType → 对应角色状态变量；未知类型返回 nullptr
        RoleState *roleStateFor(int32_t type);

        /// 守卫式接收：仅当该角色正处于 expectIng 才推进到 done 并返回 true
        bool acceptAck(int32_t type, RoleState expectIng, RoleState done);

        /// 所有存在(非 NONE)的角色是否都已到达 done 状态
        bool rolesAllIn(RoleState done) const;

        /// 把所有非 NONE 角色统一置为 s(流程结束/中止时收拢用)
        void setAllRoles(RoleState s);

        /// 是否还有任何组件存在
        bool anyRoleExists() const;

        /// 投递当前流程阶段的超时兜底消息(带 mFlowSeq，阶段推进后自动作废)
        void postFlowTimeout(int64_t delayUs);

        /// 流程阶段超时：teardown 强推收尾；seek 视为组件故障走错误收敛
        void onFlowTimeout(const std::shared_ptr<AMessage> &msg);

        /// teardown 阶段②：停数据流回执齐后释放各组件资源
        void enterTeardownReleaseStage();

        /// teardown 收尾并执行续接动作(mTeardownDone)
        void finishTeardownAndContinue();

        /// seek 超时的中止路径
        void abortSeekOnTimeout();

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

        /// 保护 mMediaClock/mMediaInfo：getCurrentPosition/getDuration 由
        /// 调用方线程直接读，与播放器线程的 prepare/finishTeardown 并发，
        /// 非原子 shared_ptr 并发读写是 UB
        mutable std::mutex mMutex;
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

        // 角色状态机
        RoleState mSourceState = ROLE_NONE;
        RoleState mAudioDecState = ROLE_NONE;
        RoleState mVideoDecState = ROLE_NONE;
        RoleState mAudioRenderState = ROLE_NONE;
        RoleState mVideoDisplayState = ROLE_NONE;

        /// 管线代次：盖在 notify 模板上，teardown 超时强推后旧组件的
        /// 迟到事件带着旧代次，无法污染新建的管线
        int32_t mPipelineGen = 0;

        /// 流程阶段序号：只用于作废自己发出的超时兜底消息
        int32_t mFlowSeq = 0;

        /// teardown 完成后的续接动作(reset 清状态 / release 回复 reply)
        std::function<void()> mTeardownDone;

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