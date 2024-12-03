#ifndef LZPLAYER_VEMEDIACLOCK_H
#define LZPLAYER_VEMEDIACLOCK_H

#include "Log.h"
#include <iostream>
#include <mutex>
#include <chrono>

class VEMediaClock {
public:
    VEMediaClock();
    ~VEMediaClock();

    // 更新音频 PTS 并计算基准时间
    void updateAudioPts(double audioPts);

    // 获取基于音频的当前时间
    double getCurrentMediaTime() const;

    // 计算视频渲染的时间
    double getVideoRenderTime(double videoPts) const;

    // 重置时钟
    void resetClock();

    // 设置播放速度
    void setPlaybackSpeed(double speed);

private:
    double m_AudioTimestamp;       // 最近更新的音频时间戳
    mutable double m_VideoTimestamp; // 最近更新的视频时间戳 (允许在 const 函数中修改)
    int64_t m_BaseSystemTime;      // 系统基准时间 (单位：微秒)
    int64_t m_AudioSystemTime;     // 音频系统时间 (单位：微秒)
    double m_PlaybackSpeed;        // 播放速度

    mutable std::mutex m_Mutex;    // 线程安全保护
    int64_t getCurrentSystemTime() const; // 获取当前系统时间
};

#endif // LZPLAYER_VEMEDIACLOCK_H