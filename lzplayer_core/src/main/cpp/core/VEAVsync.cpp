#include "VEAVsync.h"
namespace VE {
    namespace {
        /// 视频领先/落后在该范围内视为同步，直接渲染
        constexpr double kSyncThresholdUs = 40000.0;
        /// 超过该范围认为时钟不可信(刚 seek 完/音频还没起来)，退化为按帧率出帧
        constexpr double kMaxDiffUs = 500000.0;
    }

    VEAVsync::VEAVsync()
            : m_VideoPts(0), m_PlaybackSpeed(1.0), m_FrameRate(30) {
        ALOGI("VEAVsync::%s - Constructor called", __FUNCTION__);
        m_MediaClock = std::make_shared<VEMediaClock>();
    }

    VEAVsync::~VEAVsync() {
        ALOGI("VEAVsync::%s - Destructor called", __FUNCTION__);
    }

    int64_t VEAVsync::frameIntervalUs() const {
        int fps = m_FrameRate > 0 ? m_FrameRate : 30;
        return static_cast<int64_t>(1000000 / fps);
    }

    void VEAVsync::updateAudioPts(double audioPts) {
        if (m_MediaClock) {
            m_MediaClock->updateAudioPts(audioPts);
        }
    }

    void VEAVsync::updateVideoPts(double videoPts) {
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

    void VEAVsync::resetTo(double ptsUs) {
        ALOGI("VEAVsync::%s - reset clock to %f", __FUNCTION__, ptsUs);
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_VideoPts = ptsUs;
        if (m_MediaClock) {
            m_MediaClock->resetTo(ptsUs);
        }
    }

    void VEAVsync::pause() {
        if (m_MediaClock) {
            m_MediaClock->pause();
        }
    }

    void VEAVsync::resume() {
        if (m_MediaClock) {
            m_MediaClock->resume();
        }
    }

    int64_t VEAVsync::getWaitTime() const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_MediaClock) {
            return frameIntervalUs();
        }

        double diff = m_VideoPts - m_MediaClock->getCurrentMediaTime();

        if (diff > kMaxDiffUs) {
            // 时钟不可信，按帧率节奏出帧，避免长时间黑屏
            return frameIntervalUs();
        }
        if (diff > kSyncThresholdUs) {
            // 视频领先，等到该显示的时刻
            return static_cast<int64_t>(diff);
        }
        // 同步窗口内或已落后，立即渲染
        return 0;
    }

    bool VEAVsync::shouldDropFrame() const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_MediaClock) return false;

        double diff = m_VideoPts - m_MediaClock->getCurrentMediaTime();
        return diff < -kMaxDiffUs;
    }
}
