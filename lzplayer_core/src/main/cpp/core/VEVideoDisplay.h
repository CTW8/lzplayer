//
// Created by 李振 on 2025/7/24.
//

#ifndef LZPLAYER_VEVIDEODISPLAY_H
#define LZPLAYER_VEVIDEODISPLAY_H

#include "IVEComponent.h"
#include "IVideoRender.h"
#include "AHandler.h"
#include "VEVideoDecoder.h"
#include "VEAVsync.h"
#include <memory>

namespace VE {

    class VEVideoDisplay :public IVEComponent{
    public:
        VEVideoDisplay(const std::shared_ptr<AMessage> &notify,
                       const std::shared_ptr<VEAVsync> &avSync);
        ~VEVideoDisplay() override;

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult stop() override;

        VEResult seekTo(double timestampMs) override;

        VEResult flush() override;

        VEResult pause() override;

        VEResult release() override;

        VEResult setSurface(ANativeWindow *win, int width, int height);

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
            kWhatSurfaceChanged = 'surf'
        };

        std::shared_ptr<IVideoRender> m_pVideoRender = nullptr;
        std::shared_ptr<VEVideoDecoder> m_pVideoDec = nullptr;
        std::shared_ptr<AMessage> m_pNotify = nullptr;
        std::shared_ptr<VEAVsync> m_pAvSync = nullptr;
        int mViewWidth = 0;
        int mViewHeight = 0;
        int mFrameWidth = 0;
        int mFrameHeight = 0;

        bool m_IsStarted = false;
        /// 渲染/同步消息的代次，flush/seek 时递增以作废在途消息
        int32_t m_Epoch = 0;
        /// seek 后是否需要在首帧上屏时上报 FIRST_FRAME
        bool m_NotifyFirstFrame = false;
        ANativeWindow *mWin = nullptr;
    };

} // VE

#endif //LZPLAYER_VEVIDEODISPLAY_H
