//
// Created by 李振 on 2025/7/24.
//

#ifndef LZPLAYER_VEVIDEODISPLAY_H
#define LZPLAYER_VEVIDEODISPLAY_H

#include "VEVideoRenderFactory.h"
#include "utils/VEStartupTrace.h"
#include "utils/VEPerfStats.h"
#include "IFrameSink.h"
#include "IVEComponent.h"
#include "AHandler.h"
#include "VEAVsync.h"
#include <memory>
#include <deque>
#include <atomic>
#include <utility>

namespace VE {

    class VEVideoDisplay :public AHandler, public IFrameSink, public IVEComponent{
    public:
        /// 帧队列瞬时深度。可从任意线程调用(读原子镜像，见 mFrameDepth)
        int getFrameQueueDepth() const {
            return mFrameDepth.load(std::memory_order_relaxed);
        }

        VEVideoDisplay(const std::shared_ptr<AMessage> &notify,
                       const std::shared_ptr<VEAVsync> &avSync);
        ~VEVideoDisplay() override;

        /// 推模型下显示端不再持有解码器：帧由解码器 queueFrame 推进来
        /// rotationDegrees: 容器标注的画面旋转角(0/90/180/270)，竖拍视频靠它摆正
        /// frameWidth/frameHeight: 容器标注的画面尺寸。传进来是为了让渲染器
        /// 在 prepare 阶段就把纹理存储分配好——否则首帧上屏要现分配，
        /// 实测软解 1080p 首帧上屏 57.2ms 对比稳态 5.7ms，差了十倍
        VEResult prepare(ANativeWindow *win, int width, int height, int fps,
                         int rotationDegrees, int frameWidth = 0, int frameHeight = 0);

        /// IFrameSink：解码器线程调用，转投自己的 looper(盖当前队列代次)
        void queueFrame(const std::shared_ptr<VEFrame> &frame,
                        const std::shared_ptr<AMessage> &consumedReply) override;

        // IVEComponent：命令面，VEPlayer 按 Role 表统一扇出
        VEResult start() override;

        VEResult stop() override;

        VEResult seekTo(double timestampMs) override;

        VEResult flush() override;

        VEResult pause() override;

        VEResult release() override;

        VEResult setSurface(ANativeWindow *win, int width, int height);

        /// 选 Vulkan 还是 GLES。必须在 prepare 之前调用——渲染器一旦建出来
        /// 就不会因为这个开关而重建，和 setForceSles 的时机约束一致
        void setPreferVulkan(bool prefer) { mRenderPolicy.preferVulkan = prefer; }

        /// 注入启播里程碑记录器(软解路径的 T7 在此打点)
        void setStartupTrace(const std::shared_ptr<VEStartupTrace> &trace) {
            mStartupTrace = trace;
        }

        /// 注入稳态指标容器(上屏耗时、同步余量、帧队列峰值)
        void setPerfStats(const std::shared_ptr<VEPerfStats> &stats) {
            mPerfStats = stats;
        }

        /// 实际生效的后端名("Vulkan"/"OpenGL ES")。要求 Vulkan 但初始化
        /// 失败会回退，所以这里报的是事实而非意图
        const char *renderBackendName() const {
            return VEVideoRenderFactory::backendName(mUsedVulkan);
        }

        // —— 诊断计数(原子，供其它线程读) ——
        int64_t renderedFrames() const { return mRenderedFrames.load(); }
        int64_t droppedFrames() const { return mDroppedFrames.load(); }

        enum {
            kWhatEOS = 'veos',
            kWhatProgress = 'prog',
            kWhatEvent
        };

    private:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart(std::shared_ptr<AMessage> msg);

        VEResult onStop(std::shared_ptr<AMessage> msg);

        VEResult onSeekTo(double timestampMs);

        /// flush/seek 后，代次不匹配的在途渲染/同步消息应被丢弃
        bool isStaleMessage(const std::shared_ptr<AMessage> &msg) const;

        /// 投递带当前代次的同步消息
        void postSync(int64_t delayUs);

        /// 消费队首帧：投递它的回执(归还解码器 credit)并出队
        void consumeFront();

        VEResult onFlush(std::shared_ptr<AMessage> msg);

        VEResult onPause(std::shared_ptr<AMessage> msg);

        VEResult onRender(std::shared_ptr<AMessage> msg);

        VEResult onAVSync(std::shared_ptr<AMessage> msg);

        VEResult onRelease(std::shared_ptr<AMessage> msg);

        VEResult onSurfaceChanged(std::shared_ptr<AMessage> msg);

        VEResult postMessage(int32_t event,int32_t arg1,int32_t arg2,int64_t arg3,void*params);
    private:
        enum {
            kWhatPrepare = 'prep',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatSpeedRate = 'rate',
            kWhatSync = 'sync',
            kWhatRender = 'rend',
            kWhatSeek = 'seek',
            kWhatRelease = 'rele',
            kWhatPause = 'paus',
            kWhatFlush = 'flus',
            kWhatSurfaceChanged = 'surf',
            kWhatQueueFrame = 'qfrm'
        };

        std::shared_ptr<IVideoRender> m_pVideoRender = nullptr;
        /// 渲染后端策略。必须在 prepare 之前设好：渲染器在 onPrepare
        /// (或 surface 到位时)就建定了，之后改这个字段不会重建
        VEVideoRenderFactory::Policy mRenderPolicy;
        /// 实际用上的是不是 Vulkan(要求 Vulkan 但初始化失败会回退 GLES，
        /// 此处仍为假)。诊断面板显示后端名用它，不能直接显示策略里的意图
        bool mUsedVulkan = false;
        /// 启播里程碑，由 VEPlayer 注入。只在本 looper 上访问
        std::shared_ptr<VEStartupTrace> mStartupTrace;
        std::shared_ptr<VEPerfStats> mPerfStats;
        /// 上一帧上屏时刻，用于算上屏间隔(0=还没上屏过)
        int64_t mLastPresentUs = 0;
        /// 本地帧队列(帧 + 它的消费回执)。只在本 looper 上访问，无需加锁。
        /// 队列空 ⟺ 渲染链停摆，帧到达即重新拉起。
        std::deque<std::pair<std::shared_ptr<VEFrame>,
                             std::shared_ptr<AMessage>>> mFrames;
        /// mFrames 的瞬时深度镜像。mFrames 本身无锁(只在本 looper 访问)，
        /// 逐秒时间线跑在 player looper 上，直接读 deque 就是数据竞争。
        ///
        /// 在 onMessageReceived 末尾统一同步，而不是逐个 mutation 点插桩：
        /// mFrames 有 7 处增删(1 push / 2 pop / 4 clear)，逐点维护漏一处就
        /// 静默漂移，而所有增删必然发生在消息处理内部——收口在出口只需一行，
        /// 且误差上界是"一条消息"，不会累积。
        std::atomic<int> mFrameDepth{0};
        /// 队列代次：解码器线程投递时盖章(atomic 读)，flush/seek/stop 递增，
        /// 在途的旧帧到达时被丢弃
        std::atomic<int32_t> mQueueGen{0};
        std::shared_ptr<AMessage> m_pNotify = nullptr;
        std::shared_ptr<VEAVsync> m_pAvSync = nullptr;
        int mViewWidth = 0;
        int mViewHeight = 0;
        int mFrameWidth = 0;
        int mFrameHeight = 0;
        /// 容器标注的旋转角，建渲染器时传下去
        int mRotationDegrees = 0;
        /// 容器标注的画面尺寸，用于 prepare 阶段预分配纹理(0=未知，退回首帧分配)
        int mDeclaredFrameWidth = 0;
        int mDeclaredFrameHeight = 0;

        bool m_IsStarted = false;
        /// surface 被销毁时正在播放：新 surface 到来后自动复活渲染链
        bool m_SurfaceLost = false;
        /// 渲染/同步消息的代次，flush/seek 时递增以作废在途消息
        int32_t m_Epoch = 0;
        /// seek 后是否需要在首帧上屏时上报 FIRST_FRAME
        bool m_NotifyFirstFrame = false;
        /// 本段播放还没上屏过任何一帧。对齐 NuPlayer 的 mVideoSampleReceived：
        /// 起播时为真，**每次 flush/seek 后重新置真**，首帧上屏后清零。
        ///
        /// 不能用 mRenderedFrames == 0 代替：那是单调累加的诊断计数器，seek
        /// 之后早就非 0 了，于是 seek 的首帧拿不到豁免、仍要等同步——而 seek
        /// 后的首帧正是最该立刻出的那一帧(用户在等预览画面)。
        /// 也不能用 m_NotifyFirstFrame 代替：它只在 seek 路径置真，起播时恒为假。
        bool m_AwaitingFirstFrame = true;
        /// 诊断计数：累计上屏帧数与因落后被丢弃的帧数
        std::atomic<int64_t> mRenderedFrames{0};
        std::atomic<int64_t> mDroppedFrames{0};
        ANativeWindow *mWin = nullptr;
    };

} // VE

#endif //LZPLAYER_VEVIDEODISPLAY_H
