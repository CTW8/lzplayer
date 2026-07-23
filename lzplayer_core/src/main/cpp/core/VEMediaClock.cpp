#include "VEMediaClock.h"
using namespace std::chrono;
///时间单位都是微秒
namespace VE {
    VEMediaClock::VEMediaClock()
            : m_AudioTimestamp(0), m_BaseSystemTime(0), m_AudioSystemTime(0),
              m_PlaybackSpeed(1.0), m_Paused(false) {}

    VEMediaClock::~VEMediaClock() {}

    void VEMediaClock::updateAudioPts(double audioPts) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_AudioTimestamp = audioPts;
        m_AudioSystemTime = getCurrentSystemTime();
        m_Paused = false;

        if (m_BaseSystemTime == 0) {
            m_BaseSystemTime = m_AudioSystemTime;
        }
    }

    double VEMediaClock::currentMediaTimeLocked() const {
        if (m_BaseSystemTime == 0) {
            return 0.0;
        }

        // 暂停期间时钟停在最后一次更新的位置，不再外推
        if (m_Paused) {
            return m_AudioTimestamp;
        }

        int64_t currentSystemTime = getCurrentSystemTime();
        double elapsedTime = static_cast<double>(currentSystemTime - m_AudioSystemTime);
        return elapsedTime * m_PlaybackSpeed + m_AudioTimestamp;
    }

    double VEMediaClock::getCurrentMediaTime() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return currentMediaTimeLocked();
    }

    void VEMediaClock::resetClock() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_AudioTimestamp = 0;
        m_BaseSystemTime = 0;
        m_AudioSystemTime = 0;
        m_Paused = false;
    }

    void VEMediaClock::resetTo(double ptsUs, bool keepPaused) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_AudioTimestamp = ptsUs;
        m_AudioSystemTime = getCurrentSystemTime();
        m_BaseSystemTime = m_AudioSystemTime;
        if (!keepPaused) {
            m_Paused = false;
        }
    }

    bool VEMediaClock::isAnchored() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_BaseSystemTime != 0;
    }

    void VEMediaClock::pause() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Paused) {
            return;
        }
        // 先把已流逝的时间结算进基准，再冻结
        m_AudioTimestamp = currentMediaTimeLocked();
        m_AudioSystemTime = getCurrentSystemTime();
        m_Paused = true;
    }

    void VEMediaClock::resume() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Paused) {
            return;
        }
        m_AudioSystemTime = getCurrentSystemTime();
        m_Paused = false;
    }

    void VEMediaClock::setPlaybackSpeed(double speed) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // 先按旧速度结算已播放的时间，避免变速时时钟跳变
        m_AudioTimestamp = currentMediaTimeLocked();
        m_AudioSystemTime = getCurrentSystemTime();
        m_PlaybackSpeed = speed;
    }

    int64_t VEMediaClock::getCurrentSystemTime() const {
        auto now = steady_clock::now();
        return duration_cast<microseconds>(now.time_since_epoch()).count();
    }
}
