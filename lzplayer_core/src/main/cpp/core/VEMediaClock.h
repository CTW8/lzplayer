#ifndef LZPLAYER_VEMEDIACLOCK_H
#define LZPLAYER_VEMEDIACLOCK_H

#include "Log.h"
#include <iostream>
#include <mutex>
#include <chrono>
namespace VE {
    /// 以音频为主的媒体时钟：音频每渲染一帧更新一次基准，
    /// 两次更新之间由系统单调时钟外推当前播放位置。
    class VEMediaClock {
    public:
        VEMediaClock();

        ~VEMediaClock();

        // 更新音频 PTS 并计算基准时间
        void updateAudioPts(double audioPts);

        // 获取基于音频的当前时间
        double getCurrentMediaTime() const;

        // 重置时钟
        void resetClock();

        /// 重置并把时钟基准直接定位到指定位置(seek 后使用)，
        /// 否则视频侧会拿新的 pts 去和 seek 前的旧时钟比较。
        void resetTo(double ptsUs);

        /// 时钟是否已被起锚(收到过音频 pts 或 resetTo)。
        /// 纯视频文件没有音频驱动，首次起播需要手动 resetTo 起锚。
        bool isAnchored() const;

        /// 冻结/恢复外推。暂停期间若继续外推，恢复时时钟会远超实际播放位置，
        /// 导致视频帧被判定为"晚到"而大量丢弃。
        void pause();

        void resume();

        // 设置播放速度
        void setPlaybackSpeed(double speed);

    private:
        double m_AudioTimestamp;       // 最近更新的音频时间戳
        int64_t m_BaseSystemTime;      // 系统基准时间 (单位：微秒)
        int64_t m_AudioSystemTime;     // 音频系统时间 (单位：微秒)
        double m_PlaybackSpeed;        // 播放速度
        bool m_Paused;                 // 是否处于暂停(时钟冻结)状态

        mutable std::mutex m_Mutex;    // 线程安全保护
        int64_t getCurrentSystemTime() const; // 获取当前系统时间

        // 无锁版本，供已持锁的成员函数复用
        double currentMediaTimeLocked() const;
    };
}
#endif // LZPLAYER_VEMEDIACLOCK_H
