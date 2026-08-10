#ifndef LZPLAYER_VEMEDIACODECVIDEODECODER_H
#define LZPLAYER_VEMEDIACODECVIDEODECODER_H

#include <atomic>
#include <map>
#include <deque>
#include <memory>
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include "IMediaDecoder.h"
#include "IMediaSource.h"
#include "VEAVsync.h"
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
}

namespace VE {

    /// MediaCodec 硬解 + Surface 直出。
    ///
    /// **一个组件承担解码 + 同步 + 上屏**：MediaCodec 绑定显示 Surface 后，
    /// "帧"是 output buffer index，不能跨组件传递(release 必须在 codec 上做)，
    /// 所以不走 IFrameSink 帧链路。它在播放器的 Role 表里同时占据
    /// VIDEO_DECODER 与 VIDEO_RENDER 两个槽位，对上回两份回执——
    /// 于是 VEPlayer 的 seek 三阶段与 teardown 两阶段完全不需要区分软硬解。
    ///
    /// 上屏时机仍由 VEAVsync 判定，只是动作从"GL 上传 + swap"变成
    /// releaseOutputBuffer(render=true)，零拷贝零纹理上传。
    ///
    /// 驱动模型用同步 API + 自身 looper 的工作循环(而非 API 28 的异步回调)：
    /// minSdk 24 下异步回调需要 dlsym 兜底，收益不抵复杂度；同步 API 从
    /// API 21 起可用，硬解因此覆盖全部支持机型。
    class VEMediaCodecVideoDecoder : public AHandler, public IMediaDecoder {
    public:
        explicit VEMediaCodecVideoDecoder(std::shared_ptr<AMessage> &notify,
                                          const std::shared_ptr<VEAVsync> &avSync);
        ~VEMediaCodecVideoDecoder() override;

        /// params 需带 "surface"(ANativeWindow*)。sink 不使用(直出 Surface)。
        void setStartupTrace(const std::shared_ptr<VEStartupTrace> &trace) override {
            mStartupTrace = trace;
        }

        void setPerfStats(const std::shared_ptr<VEPerfStats> &stats) override {
            mPerfStats = stats;
        }

        VEResult prepare(std::shared_ptr<IMediaSource> source,
                         std::shared_ptr<IFrameSink> sink,
                         const VEBundle &params) override;

        // IVEComponent
        VEResult start() override;
        VEResult stop() override;
        VEResult pause() override;
        VEResult seekTo(double timestampMs) override;
        VEResult flush() override;
        VEResult release() override;

        /// surface 变化(销毁/重建/转屏)。null 表示 surface 已失效。
        VEResult setSurface(ANativeWindow *win);

        /// 能否为该轨道创建硬解。工厂在建对象前调用，失败就走软解。
        static bool isSupported(const VETrackInfo &track);

        // —— 诊断计数(原子，供其它线程读) ——
        int64_t renderedFrames() const { return mRenderedFrames.load(); }
        int64_t droppedFrames() const { return mDroppedFrames.load(); }

    private:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        VEResult onPrepare(const std::shared_ptr<AMessage> &msg);
        VEResult onStart();
        VEResult onPause();
        VEResult onStop();
        VEResult onFlush();
        VEResult onSeek(double timestampMs);
        VEResult onRelease();
        VEResult onSurfaceChanged(ANativeWindow *win);

        /// 一轮工作：喂输入 + 收输出 + 排上屏
        void onDoWork();
        /// 到点把队首 output buffer 送上屏
        void onRenderOut(const std::shared_ptr<AMessage> &msg);

        /// 创建并配置 codec(含 csd 与 rotation)
        VEResult configureCodec();
        void destroyCodec();
        /// AVCC/HVCC → Annex-B 的 bitstream filter
        VEResult setupBitstreamFilter();
        void destroyBitstreamFilter();

        /// 喂一个包进 codec；返回 false 表示这轮没喂进去
        bool feedInput();
        /// 取一个已解帧；返回 false 表示暂时没有输出
        bool drainOutput();
        /// 按 AVSync 安排队首帧的上屏时刻
        void scheduleRender();

        void postDoWork(int64_t delayUs);
        /// 上报组件事件；type 决定这条回执算在哪个角色头上
        void postEvent(int32_t componentType, int32_t event,
                       int32_t arg1, int64_t arg3);
        /// 解码器与显示两个角色都要回执的命令(pause/stop/seek/release)
        void postEventBothRoles(int32_t event, int32_t arg1 = 0);

        /// 运行期硬解故障：上报 ERROR 并附带 fallback 标记，
        /// 由播放器走"重建为软解"的路径
        void reportFatal(const char *reason);

        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatFlush = 'flus',
            kWhatSeek = 'seek',
            kWhatRelease = 'rele',
            kWhatDoWork = 'work',
            kWhatRenderOut = 'rout',
            kWhatSurface = 'surf',
        };

        /// 等待上屏的输出缓冲
        struct OutBuffer {
            ssize_t index;
            int64_t ptsUs;
        };

        static const int64_t kNoSeekTarget = INT64_MIN;

        std::shared_ptr<AMessage> mNotify;
        std::shared_ptr<VEAVsync> mAVSync;
        std::shared_ptr<IMediaSource> mSource;

        AMediaCodec *mCodec = nullptr;
        AVBSFContext *mBsf = nullptr;
        ANativeWindow *mWindow = nullptr;
        VETrackInfo mTrack;

        std::deque<OutBuffer> mOutQueue;
        /// 上一轮没喂进去的包，下轮优先
        std::shared_ptr<VEPacket> mPendingPacket;

        bool mCodecReady = false;
        bool mIsStarted = false;
        bool mInputEOS = false;
        bool mOutputEOS = false;
        /// 已经有一条在途的上屏消息，别重复排
        bool mRenderPending = false;
        /// seek 后首帧上屏要回 FIRST_FRAME
        bool mNotifyFirstFrame = false;
        /// 启播里程碑，由 VEPlayer 注入。硬解把解码与上屏合在一个组件里，
        /// 所以 T4a / T6 / T7 三个点都在这里打
        std::shared_ptr<VEStartupTrace> mStartupTrace;
        std::shared_ptr<VEPerfStats> mPerfStats;
        int64_t mLastPresentUs = 0;
        /// pts → 入队时刻(us)。硬解的 dequeueOutputBuffer 是 timeout 0 的
        /// 非阻塞轮询，直接测那个调用只会得到近似 0 的无意义数字，所以改测
        /// "入队到出队"的端到端延迟。**这个口径与软解的 send/receive 往返
        /// 不可直接互比**，报告需带解码路径脚注。
        ///
        /// 有界：超过 kMaxPendingTimes 条就丢最旧的。flush/seek 必须清空，
        /// 否则被丢弃的那些包会永久占位，并让后续样本配错时刻。
        std::map<int64_t, int64_t> mFeedTimeUs;
        static constexpr size_t kMaxPendingTimes = 64;
        /// surface 失效期间：继续解码但丢弃输出，不画向废窗口
        bool mSurfaceLost = false;

        int32_t mEpoch = 0;
        int64_t mSeekTargetUs = kNoSeekTarget;
        std::atomic<int64_t> mRenderedFrames{0};
        std::atomic<int64_t> mDroppedFrames{0};
    };
}

#endif //LZPLAYER_VEMEDIACODECVIDEODECODER_H
