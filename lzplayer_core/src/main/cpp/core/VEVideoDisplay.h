//
// Created by 李振 on 2025/7/24.
//

#ifndef LZPLAYER_VEVIDEODISPLAY_H
#define LZPLAYER_VEVIDEODISPLAY_H

#include "IVideoRender.h"
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
        VEVideoDisplay(const std::shared_ptr<AMessage> &notify,
                       const std::shared_ptr<VEAVsync> &avSync);
        ~VEVideoDisplay() override;

        /// 推模型下显示端不再持有解码器：帧由解码器 queueFrame 推进来
        /// rotationDegrees: 容器标注的画面旋转角(0/90/180/270)，竖拍视频靠它摆正
        VEResult prepare(ANativeWindow *win, int width, int height, int fps,
                         int rotationDegrees);

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
        /// 本地帧队列(帧 + 它的消费回执)。只在本 looper 上访问，无需加锁。
        /// 队列空 ⟺ 渲染链停摆，帧到达即重新拉起。
        std::deque<std::pair<std::shared_ptr<VEFrame>,
                             std::shared_ptr<AMessage>>> mFrames;
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

        bool m_IsStarted = false;
        /// surface 被销毁时正在播放：新 surface 到来后自动复活渲染链
        bool m_SurfaceLost = false;
        /// 渲染/同步消息的代次，flush/seek 时递增以作废在途消息
        int32_t m_Epoch = 0;
        /// seek 后是否需要在首帧上屏时上报 FIRST_FRAME
        bool m_NotifyFirstFrame = false;
        /// 诊断计数：累计上屏帧数与因落后被丢弃的帧数
        std::atomic<int64_t> mRenderedFrames{0};
        std::atomic<int64_t> mDroppedFrames{0};
        ANativeWindow *mWin = nullptr;
    };

} // VE

#endif //LZPLAYER_VEVIDEODISPLAY_H
