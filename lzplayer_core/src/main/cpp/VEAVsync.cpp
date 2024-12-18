#include "VEAVsync.h"

VEAVsync::VEAVsync()
    : m_VideoPts(0), m_PlaybackSpeed(1.0), m_FrameRate(30) {
    m_MediaClock = std::shared_ptr<VEMediaClock>();
}

VEAVsync::~VEAVsync() {}

void VEAVsync::updateAudioPts(double audioPts) {
    if (m_MediaClock) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MediaClock->updateAudioPts(audioPts);
    }
}

void VEAVsync::updateVideoPts(double videoPts) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_VideoPts = videoPts;
}

void VEAVsync::setPlaybackSpeed(double speed) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_PlaybackSpeed = speed;
    if (m_MediaClock) {
        m_MediaClock->setPlaybackSpeed(speed);
    }
}

void VEAVsync::setFrameRate(int frameRate) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_FrameRate = frameRate;
}

int64_t VEAVsync::getWaitTime() const {
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_MediaClock) return static_cast<int64_t>(1000000 / m_FrameRate);

    double audioTime = m_MediaClock->getCurrentMediaTime();
    double diff = (m_VideoPts - audioTime) / m_PlaybackSpeed;

    if (diff > 0) {
        if (diff <= 0.04) {
            return static_cast<int64_t>(diff * 1000000);
        } else if (diff <= 0.5) {
            return static_cast<int64_t>(diff * 1000000);
        } else {
            return static_cast<int64_t>(1000000 / m_FrameRate);
        }
    } else {
        if (diff >= -0.04) {
            return 0;
        } else if (diff >= -0.5) {
            return static_cast<int64_t>(1000000 / m_FrameRate);
        } else {
            return static_cast<int64_t>(1000000 / m_FrameRate);
        }
    }
}

bool VEAVsync::shouldDropFrame() const {
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_MediaClock) return false;

    double audioTime = m_MediaClock->getCurrentMediaTime();
    double diff = (m_VideoPts - audioTime) / m_PlaybackSpeed;

    return diff < -0.5;
}