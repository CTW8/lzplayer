#include "VEAVsync.h"

VEAVsync::VEAVsync()
    : m_VideoPts(0), m_PlaybackSpeed(1.0), m_FrameRate(30) {
    ALOGI("VEAVsync::%s - Constructor called", __FUNCTION__);
    m_MediaClock = std::make_shared<VEMediaClock>();
}

VEAVsync::~VEAVsync() {
    ALOGI("VEAVsync::%s - Destructor called", __FUNCTION__);
}

void VEAVsync::updateAudioPts(double audioPts) {
    ALOGI("VEAVsync::%s - Updating audio PTS: %f", __FUNCTION__, audioPts);
    if (m_MediaClock) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MediaClock->updateAudioPts(audioPts);
    }
}

void VEAVsync::updateVideoPts(double videoPts) {
    ALOGI("VEAVsync::%s - Updating video PTS: %f", __FUNCTION__, videoPts);
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_VideoPts = videoPts;
}

void VEAVsync::setPlaybackSpeed(double speed) {
    ALOGI("VEAVsync::%s - Setting playback speed: %f", __FUNCTION__, speed);
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_PlaybackSpeed = speed;
    if (m_MediaClock) {
        m_MediaClock->setPlaybackSpeed(speed);
    }
}

void VEAVsync::setFrameRate(int frameRate) {
    ALOGI("VEAVsync::%s - Setting frame rate: %d", __FUNCTION__, frameRate);
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_FrameRate = frameRate;
}

int64_t VEAVsync::getWaitTime() const {
    ALOGI("VEAVsync::%s - Calculating wait time", __FUNCTION__);
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_MediaClock){
        ALOGI("VEAVsync::%s - Media clock is null, returning frame rate wait time", __FUNCTION__);
        return static_cast<int64_t>(1000000 / m_FrameRate);
    }

    double audioTime = m_MediaClock->getCurrentMediaTime();
    double diff = m_VideoPts - audioTime;
    ALOGI("VEAVsync::%s - audioTime: %f, m_VideoPts: %f, diff: %f", __FUNCTION__, audioTime, m_VideoPts, diff);
    if (diff > 0) {
        if (diff <= 40000) {
            return static_cast<int64_t>(diff);
        } else if (diff <= 500000) {
            return static_cast<int64_t>(diff);
        } else {
            return static_cast<int64_t>(1000000 / m_FrameRate);
        }
    } else {
        if (diff >= -40000) {
            return 0;
        } else if (diff >= -500000) {
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
    double diff = m_VideoPts - audioTime;
    ALOGI("VEAVsync::%s - audioTime: %f, m_VideoPts: %f, diff: %f", __FUNCTION__, audioTime, m_VideoPts, diff);

    return diff < -500000;
}