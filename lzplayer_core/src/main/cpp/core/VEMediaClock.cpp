#include "VEMediaClock.h"
using namespace std::chrono;
///时间单位都是微秒
namespace VE {
    VEMediaClock::VEMediaClock()
            : m_AudioTimestamp(0), m_VideoTimestamp(0), m_BaseSystemTime(0), m_AudioSystemTime(0),
              m_PlaybackSpeed(1.0) {}

    VEMediaClock::~VEMediaClock() {}

    void VEMediaClock::updateAudioPts(double audioPts) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_AudioTimestamp = audioPts;
        m_AudioSystemTime = getCurrentSystemTime();

        if (m_BaseSystemTime == 0) {
            m_BaseSystemTime = m_AudioSystemTime;
        }
    }

    double VEMediaClock::getCurrentMediaTime() const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_BaseSystemTime == 0) {
            return 0.0;
        }

        int64_t currentSystemTime = getCurrentSystemTime();
        double elapsedTime = (static_cast<double>(currentSystemTime - m_AudioSystemTime));
        ALOGI("MediaClock::%s - Current elapsed time: %f, m_AudioTimestamp: %f", __FUNCTION__,
              elapsedTime * m_PlaybackSpeed, m_AudioTimestamp);

        return elapsedTime * m_PlaybackSpeed + m_AudioTimestamp;
    }

    double VEMediaClock::getVideoRenderTime(double videoPts) const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_VideoTimestamp = videoPts;
        return getCurrentMediaTime() - videoPts;
    }

    void VEMediaClock::resetClock() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        m_AudioTimestamp = 0;
        m_VideoTimestamp = 0;
        m_BaseSystemTime = 0;
        m_AudioSystemTime = 0;
    }

    void VEMediaClock::setPlaybackSpeed(double speed) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PlaybackSpeed = speed;
    }

    int64_t VEMediaClock::getCurrentSystemTime() const {
        auto now = steady_clock::now();
        return duration_cast<microseconds>(now.time_since_epoch()).count();
    }
}