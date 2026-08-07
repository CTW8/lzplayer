#include "VEAVsync.h"
namespace VE {
    namespace {
        /// 视频领先/落后在该范围内视为同步，直接渲染
        constexpr double kSyncThresholdUs = 40000.0;
        /// 落后超过该值就丢帧追赶。原来用 500ms 才丢，落后 40~500ms 区间
        /// 既不丢帧也无追赶手段，会长期维持可感知的音画不同步
        constexpr double kDropThresholdUs = 100000.0;
        /// 超过该范围认为时钟不可信(刚 seek 完/音频还没起来)，退化为按帧率出帧
        constexpr double kMaxDiffUs = 500000.0;
        constexpr int kDefaultFrameRate = 30;
    }

    VEAVsync::VEAVsync(const std::shared_ptr<VEMediaClock> &clock)
            : m_MediaClock(clock), m_VideoPts(0), m_FrameRate(kDefaultFrameRate) {
        ALOGI("VEAVsync::%s - Constructor called", __FUNCTION__);
    }

    VEAVsync::~VEAVsync() {
        ALOGI("VEAVsync::%s - Destructor called", __FUNCTION__);
    }

    int64_t VEAVsync::frameIntervalUs() const {
        int fps = m_FrameRate > 0 ? m_FrameRate : kDefaultFrameRate;
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

    void VEAVsync::setFrameRate(int frameRate) {
        ALOGI("VEAVsync::%s - Setting frame rate: %d", __FUNCTION__, frameRate);
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FrameRate = frameRate;
    }

    void VEAVsync::reset(double ptsUs) {
        // seek 后清掉上一段的视频 pts，避免拿旧值和新时钟比较
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_VideoPts = ptsUs;
    }

    int64_t VEAVsync::getWaitTime() const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_MediaClock) {
            return frameIntervalUs();
        }

        double diff = m_VideoPts - m_MediaClock->getCurrentMediaTime();

        if (diff > kMaxDiffUs) {
            // 时钟不可信，按帧率节奏出帧，避免长时间黑屏。
            // 这是实时节奏，不受速率影响(时钟本来就不可信)
            return frameIntervalUs();
        }
        if (diff > kSyncThresholdUs) {
            // 视频领先，等到该显示的时刻。diff 是媒体时间差，而消息延时
            // 等的是真实时间——时钟以 speed 倍外推，媒体差就以 speed 倍
            // 速率收敛，因此真实等待 = 媒体差 / speed。
            const double speed = m_MediaClock->getPlaybackSpeed();
            return static_cast<int64_t>(diff / (speed > 0 ? speed : 1.0));
        }
        // 同步窗口内或已落后，立即渲染
        return 0;
    }

    int64_t VEAVsync::getLastDiffUs() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_MediaClock) {
            return 0;
        }
        return static_cast<int64_t>(m_VideoPts - m_MediaClock->getCurrentMediaTime());
    }

    bool VEAVsync::shouldDropFrame() const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_MediaClock) return false;

        double diff = m_VideoPts - m_MediaClock->getCurrentMediaTime();
        return diff < -kDropThresholdUs;
    }
}
