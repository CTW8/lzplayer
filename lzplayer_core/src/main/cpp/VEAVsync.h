#ifndef __VE_AVSYNC__
#define __VE_AVSYNC__

#include "VEMediaClock.h"
#include <cmath>
#include <mutex>

class VEAVsync {
public:
    VEAVsync();
    ~VEAVsync();

    // 更新音频 PTS
    void updateAudioPts(double audioPts);

    // 更新视频 PTS
    void updateVideoPts(double videoPts);

    // 设置播放速度
    void setPlaybackSpeed(double speed);

    // 设置帧率
    void setFrameRate(int frameRate);

    // 获取等待时间
    int64_t getWaitTime() const;

    // 判断是否需要丢帧
    bool shouldDropFrame() const;

private:
    std::shared_ptr<VEMediaClock> m_MediaClock; // 媒体时钟
    double m_VideoPts;          // 当前视频 PTS
    double m_PlaybackSpeed;     // 播放速度
    int m_FrameRate;            // 帧率
    mutable std::mutex m_Mutex;         // 线程安全保护
};

#endif