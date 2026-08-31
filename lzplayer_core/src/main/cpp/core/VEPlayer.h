#ifndef __VEPLAYER__
#define __VEPLAYER__

#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <deque>
#include <array>
#include <android/native_window_jni.h>
#include "VESource.h"
#include "IVEComponent.h"
#include "IMediaDecoder.h"
#include "VEVideoDecoderFactory.h"
#include "VEAudioDecoder.h"
#include "VEVideoDecoder.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEPacketQueue.h"
#include "VEError.h"
#include "jni.h"
#include "thread/AHandler.h"
#include "VEDef.h"
#include "VEAudioRender.h"
#include "VEVideoDisplay.h"
#include "utils/VESeekTrace.h"
#include "VESubtitleTrack.h"
#include <map>


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

        /// 轨道列表(JSON 数组字符串，供 JNI 直接回传 Java 解析)。
        /// 可跨线程调用，prepare 之后有效。
        std::string getTrackInfoJson();

        /// 切换活跃轨道。音轨切换会做一次全链精准 seek 回当前位置
        /// (画面短暂定格，不黑屏)；字幕轨切换是轻量路径。
        VEResult selectTrack(int trackIndex);

        /// 关闭指定轨道(目前只对字幕轨有意义)
        VEResult deselectTrack(int trackIndex);

        /// 加载外挂字幕文件(.srt/.ass)，成功后作为虚拟轨出现在轨道列表里
        VEResult addExternalSubtitle(const std::string &path);

        /// 运行期统计快照(JSON)。诊断面板按进度 tick 的节奏拉取，
        /// 可跨线程调用——内部取的都是原子量或自带锁的对象。
        std::string getStatsJson();

        /// 启播链路里程碑 JSON。独立 getter 而非塞进 getStatsJson()：
        /// 后者每个进度 tick 都会被解析，而这份数据一次启播只变一次
        std::string getStartupTraceJson();

        /// 最近 10 次 seek 的三阶段耗时与精度 JSON
        std::string getSeekTraceJson();

        /// 变速与切轨的三阶段追踪，一次取回：{"speed":{...},"track":{...}}。
        /// 合成一个 getter 而不是两个 JNI 方法 —— 两者总是一起看。
        std::string getSwitchTraceJson();

        /// 测试开关：强制软解 / 强制 OpenSL ES。
        /// 改的是"下次建链"的策略，当前管线不受影响，需重新 prepare 才生效。
        void setForceSoftwareDecoder(bool force);
        void setForceSlesAudio(bool force);
        /// 软解显示端用 Vulkan 还是 GLES。硬解直出 Surface，不受此开关影响
        void setPreferVulkanRender(bool prefer);

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
                // 起播瞬间时钟会因设备延迟补偿短暂为负，对上层没有意义，夹住
                onProgressCallback((double) (progress > 0 ? progress : 0) * 1000.f / AV_TIME_BASE);
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

        /// 媒体源工厂：按路径(未来按 scheme)构造具体 VESource 实现。
        /// 当前仅本地文件源(VEDemux)。返回的源尚未 registerHandler。
        std::shared_ptr<VESource> createSource(const std::string &path);

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
            kWhatProgressTick = 'prgT',
            kWhatSetSpeed = 'sspd',
            kWhatSelectTrack = 'sltk',
            kWhatAddSubtitle = 'adsb'
        };

        /// 播放器内部状态。与 VEPlayerDriver 的状态机职责不同：
        /// Driver 负责校验 Java 层 API 调用是否合法，这里负责编排内部流程。
        enum PlayerState {
            STATE_IDLE,
            STATE_PREPARING,   ///< demux 异步 prepare 进行中，属长流程(Flow)
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

        /// 管线中的角色槽位。命令扇出与回执状态机都按这张表遍历，
        /// 新增组件(硬解解码器、字幕轨)只要占一个槽位就自动参与
        /// seek/teardown 的分阶段握手，不必改动任何编排代码。
        enum RoleIndex {
            ROLE_IDX_VIDEO_DISPLAY = 0,   ///< 拆解顺序：先停下游再停上游
            ROLE_IDX_AUDIO_RENDER,
            ROLE_IDX_VIDEO_DECODER,
            ROLE_IDX_AUDIO_DECODER,
            ROLE_IDX_SUBTITLE,
            ROLE_IDX_SOURCE,
            kRoleCount
        };

        struct Role {
            /// 组件本体；nullptr 表示该角色在当前管线中不存在
            std::shared_ptr<IVEComponent> comp;
            /// 组件所在的 looper(拆解收尾时统一停掉)
            std::shared_ptr<ALooper> looper;
            /// 回执守卫用的角色状态
            RoleState state = ROLE_NONE;
            /// 组件在 notify 里自报的类型，用于把回执映射回本槽位
            int32_t componentType = EComponentType::E_COMPONENT_TYPE_UNKNOW;
        };

        VEResult onSetDataSource(std::shared_ptr<AMessage> msg);

        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart(std::shared_ptr<AMessage> msg);

        VEResult onStop(std::shared_ptr<AMessage> msg);

        VEResult onPause(std::shared_ptr<AMessage> msg);

        VEResult onSeek(std::shared_ptr<AMessage> msg);

        VEResult onReset(std::shared_ptr<AMessage> msg);

        VEResult onRelease(std::shared_ptr<AMessage> msg);

        VEResult onSetSpeed(const std::shared_ptr<AMessage> &msg);

        /// setPlaySpeed 的实际执行体(排队解耦后从 onSetSpeed 拆出)
        VEResult doSetSpeed(float speed);

        /// 建视频链(解码器 + 显示)。硬解时二者是同一个组件。
        VEResult setupVideoChain();

        VEResult onSelectTrack(const std::shared_ptr<AMessage> &msg);
        VEResult doSelectTrack(int trackIndex, bool deselect);

        /// 切轨追踪的起点/终点(定义与采样时刻见 .cpp)
        void beginTrackTrace(int trackIndex);
        void finishTrackTrace(int64_t ptsUs, bool aborted);
        VEResult onAddSubtitle(const std::shared_ptr<AMessage> &msg);

        /// 建字幕链(首次选中字幕轨时才创建)
        VEResult setupSubtitleChain();
        /// 音轨切换：重建音频链(codec 变了)并全链 seek 回当前位置
        VEResult switchAudioTrack(int trackIndex);

        /// 硬解运行期故障后的重建：拆掉视频链，强制软解重建，
        /// 再 seek 回当前位置续播。整个过程走 PendingAction 串行化。
        void rebuildVideoAsSoftware();

        VEResult onSurfaceChanged(ANativeWindow *win,int viewWidth,int viewHeight);

        VEResult onComponentEvent(const std::shared_ptr<AMessage> &msg);

        // ---- 角色状态机：Role 表驱动，回执按角色守卫接收 ----

        /// 把组件登记进角色槽位(同时记下它的 looper 与 notify 类型)
        void setRole(RoleIndex idx, const std::shared_ptr<IVEComponent> &comp,
                     const std::shared_ptr<ALooper> &looper, int32_t componentType);

        /// 对所有存在的组件依次执行 fn(按 Role 表顺序 = 拆解顺序)
        void forEachRole(const std::function<void(Role &)> &fn);

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

        /// 收敛到静止态(NuPlayer 的 error 即收敛原则)：停 tick、停时钟、
        /// 显式 stop 所有组件、清 seek 流程状态。ERROR/超时/完成时使用。
        void converge();

        // ---- 操作串行化(仿 NuPlayer mDeferredActions)：长流程不重叠 ----

        /// 会引发多阶段流程的操作。流程忙时入队，前一个完成后按序执行。
        struct PendingAction {
            enum Type {
                ACTION_SEEK,
                ACTION_SET_DATA_SOURCE,
                ACTION_PREPARE,
                ACTION_RESET,
                ACTION_RELEASE,
                ACTION_SET_SPEED,
                ACTION_REBUILD_VIDEO,
                ACTION_SELECT_TRACK
            } type = ACTION_SEEK;
            double seekMs = 0;                     ///< ACTION_SEEK
            float speed = 1.0f;                    ///< ACTION_SET_SPEED
            int trackIndex = -1;                   ///< ACTION_SELECT_TRACK
            bool deselect = false;                 ///< ACTION_SELECT_TRACK
            std::string path;                      ///< ACTION_SET_DATA_SOURCE
            std::shared_ptr<AReplyToken> reply;    ///< ACTION_RELEASE
            bool wantsReply = false;               ///< ACTION_RELEASE
        };

        /// 是否有长流程在途(seek/teardown；prepare 在步骤8改造后也计入)
        bool isFlowBusy() const;

        /// 弹出并执行排队的操作，直到队空或某个操作开启了新流程
        void processPendingActions();

        /// 丢弃队列中所有 SEEK(reset/release/stop 使其失去意义)
        void dropQueuedSeeks();

        /// RESET/RELEASE 请求中止当前 seek：当前阶段回执到齐后走这里
        void abortSeekForAction();

        /// prepare 的实际执行体(排队解耦后从 onPrepare 拆出)：
        /// 发起 demux 异步 prepare 后即返回
        VEResult doPrepare();

        /// demux 回 PREPARE_DONE 后的建链后半段
        VEResult continuePrepare();

        /// reset/release 的实际执行体
        void executeReset();
        void executeRelease(const std::shared_ptr<AReplyToken> &reply, bool wantsReply);

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
        std::shared_ptr<VESource> mSource = nullptr;
        std::shared_ptr<ALooper> mSourceLooper = nullptr;
        std::shared_ptr<VEAudioDecoder> mAudioDecoder = nullptr;
        std::shared_ptr<ALooper> mAudioDecodeLooper = nullptr;
        /// 视频解码器按接口持有：软解 VEVideoDecoder 与硬解
        /// VEMediaCodecVideoDecoder 由工厂选择，播放器不区分
        std::shared_ptr<IMediaDecoder> mVideoDecoder = nullptr;
        std::shared_ptr<ALooper> mVideoDecodeLooper = nullptr;
        /// 当前视频链走的是硬解(硬解组件兼任显示，占两个角色槽位)
        bool mVideoHardware = false;
        /// 解码器选择策略(可由上层强制软/硬解)
        DecoderPolicy mDecoderPolicy;
        /// 强制音频走 SLES(测试开关，建链时传给 VEAudioRender)
        std::atomic<bool> mForceSlesAudio{false};
        std::atomic<bool> mPreferVulkanRender{false};
        /// 用户显式要求的强制软解。与 mDecoderPolicy.forceSoftware 分开存：
        /// 后者会被运行期 fallback 置位，重建时不能把用户意图弄丢。
        std::atomic<bool> mUserForceSoftware{false};
        std::shared_ptr<VEVideoDisplay> mVideoRender = nullptr;
        std::shared_ptr<ALooper> mVideoRenderLooper = nullptr;
        /// 主时钟由播放器持有：启停/定位是播放流程的一部分，
        /// VEAVsync 只拿它做同步判定，不负责它的生命周期
        std::shared_ptr<VEMediaClock> mMediaClock = nullptr;
        std::shared_ptr<VEAVsync> mAVSync = nullptr;

        std::shared_ptr<AMessage> mRenderNotifyMsg = nullptr;

        std::shared_ptr<VEAudioRender> mAudioOutput = nullptr;
        std::shared_ptr<ALooper> mAudioOutputLooper = nullptr;

        /// 字幕轨组件。默认不创建，首次 selectTrack(字幕) 才建。
        std::shared_ptr<VESubtitleTrack> mSubtitle = nullptr;
        std::shared_ptr<ALooper> mSubtitleLooper = nullptr;
        /// 外挂字幕：解析好的 cue 按虚拟轨号存着，选中时喂给组件
        std::map<int, std::vector<VESubtitleTrack::Cue>> mExternalCues;
        int mNextExternalTrackIndex = kExternalTrackIndexBase;

        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;
        /// 启播里程碑。跨多个 looper 写入，自身加锁；shared_ptr 在
        /// doPrepare 时就建好并分发下去，之后不再换对象(只 reset 内容)，
        /// 免得各组件持有的指针失效
        std::shared_ptr<VEStartupTrace> mStartupTrace;
        /// 稳态指标。与 mStartupTrace 同样在 doPrepare 时建好并分发，
        /// 之后只 reset 内容不换对象
        std::shared_ptr<VEPerfStats> mPerfStats;
        /// 逐秒时间线状态(上一秒的累计快照)。只在 player looper 上访问
        VEPerfStats::Timeline mTimeline;
        /// seek 三阶段耗时与精度，环形缓冲留最近 10 次
        std::shared_ptr<VESeekTrace> mSeekTrace;
        /// 变速追踪(kind="speed", param=目标速率)。两段有效, 第三段恒为 null,
        /// 原因见 VESeekTrace::endAtStage2 的注释。
        std::shared_ptr<VESeekTrace> mSpeedTrace;
        /// 切轨追踪(kind="track", param=目标轨道号)。三段齐全 ——
        /// 音轨切换以 switchAudioTrack 末尾的全链 seek 作为第三段,
        /// 它的终点是真实的 FIRST_FRAME 事件。
        std::shared_ptr<VESeekTrace> mTrackTrace;
        /// 切轨的第三段正等着内嵌的那次 seek 完成。**不能用 mTrackTrace 是否
        /// inFlight 代替**: 字幕轨那几条路径不发起 seek, 已在 stage2 结算完毕。
        bool mTrackSwitchAwaitingSeek = false;
        /// 变速的第②段正等音频渲染回 SPEED_APPLIED。无音轨时不设，
        /// 否则那次记录会挂着等一个永不到来的事件、永远不入库。
        bool mSpeedAwaitingApply = false;
        /// 下一次流程超时的一次性覆盖值(微秒), 0=用默认。
        /// 回退续播要多做一次建链, 见 kFallbackSeekTimeoutUs
        int64_t mSeekTimeoutOverrideUs = 0;

        std::string mPath;

        bool mVideoEOS = false;
        bool mAudioEOS = false;

        /// setLooping 由调用方线程写入，播放器线程读取
        std::atomic<bool> mEnableLoop{false};

        /// 当前播放速率(仅播放器线程读写)
        float mPlaybackSpeed = 1.0f;

        /// 网络缓冲不足导致的内部暂停。注意它不改 mState——对外状态
        /// 仍然是 STARTED，用户看到的是"卡住了"而不是"被暂停了"。
        bool mBuffering = false;

        PlayerState mState = STATE_IDLE;

        /// 角色表：命令扇出与回执状态机的唯一依据
        std::array<Role, kRoleCount> mRoles;

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

        // 操作串行化
        std::deque<PendingAction> mPendingActions;
        /// RESET/RELEASE 入队时置位：当前 seek 阶段结束即中止，不进下一阶段
        bool mAbortSeek = false;

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