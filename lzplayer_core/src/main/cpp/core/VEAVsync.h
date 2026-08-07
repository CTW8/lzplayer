#ifndef __VE_AVSYNC__
#define __VE_AVSYNC__

#include "VEMediaClock.h"
#include <cmath>
#include <memory>
#include <mutex>

namespace VE {
    /// 音视频同步策略。
    ///
    /// 只负责"这一帧该等多久 / 该不该丢"的判定，时间基准来自外部注入的
    /// VEMediaClock —— 时钟的启停和定位由播放器统一掌管，这里只读不写。
    class VEAVsync {
    public:
        explicit VEAVsync(const std::shared_ptr<VEMediaClock> &clock);

        ~VEAVsync();

        // 更新音频 PTS(推进主时钟)
        void updateAudioPts(double audioPts);

        // 更新视频 PTS
        void updateVideoPts(double videoPts);

        // 设置帧率
        void setFrameRate(int frameRate);

        /// seek 后把同步基准挪到目标位置(时钟本身由播放器负责重置)
        void reset(double ptsUs);

        // 获取等待时间
        int64_t getWaitTime() const;

        // 判断是否需要丢帧
        bool shouldDropFrame() const;

        /// 最近一次判定时的"视频 pts − 主时钟"(微秒)。正=视频领先。
        /// 诊断面板用它显示音视频偏移。
        int64_t getLastDiffUs() const;

    private:
        int64_t frameIntervalUs() const;

        /// 主时钟，由 VEPlayer 创建并持有，这里只是引用
        std::shared_ptr<VEMediaClock> m_MediaClock;
        double m_VideoPts;          // 当前视频 PTS
        int m_FrameRate;            // 帧率
        mutable std::mutex m_Mutex; // 保护上面两个同步策略状态
    };
}
#endif
