#include "VEAudioRender.h"

namespace VE {
    VEAudioRender::VEAudioRender(const std::shared_ptr<IAudioRender>& audioRenderer,
                                 const std::shared_ptr<VEAudioDecoder>& audioDecoder)
            : m_AudioRenderer(audioRenderer), m_AudioDecoder(audioDecoder) {}

    VEAudioRender::~VEAudioRender() {

    }

    VEResult VEAudioRender::prepare(const AudioConfig &config) {
        if (m_AudioRenderer) {
            m_AudioRenderer->configure(config);
            return m_AudioRenderer->init();
        }
        return -1;
    }

    VEResult VEAudioRender::start() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioRender::stop() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioRender::prepare(VEBundle params) {
        if (m_AudioRenderer) {
            m_AudioRenderer->configure(params.get<AudioConfig>("config"));
            return m_AudioRenderer->init();
        }
        return 0;
    }

    VEResult VEAudioRender::seekTo(uint64_t timestamp) {
        return 0;
    }

    VEResult VEAudioRender::flush() {
        return 0;
    }

    VEResult VEAudioRender::pause() {
        return 0;
    }

    VEResult VEAudioRender::release() {
        return 0;
    }

    void VEAudioRender::renderFrame(std::shared_ptr<VEFrame> frame) {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_FrameQueue.push_back(frame);
        m_Cond.notify_one();
    }

    void VEAudioRender::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        switch (msg->what()) {
            case kWhatInit:
                prepare(AudioConfig{44100, 2, 16}); // 示例配置
                break;
            case kWhatStart:
                if (m_AudioRenderer) {
                    m_AudioRenderer->start();
                }
                std::make_shared<AMessage>(kWhatFetchData, shared_from_this())->post();
                break;
            case kWhatStop:
                if (m_AudioRenderer) {
                    m_AudioRenderer->stop();
                }
                break;
            case kWhatFetchData:
                fetchAudioData();
                std::make_shared<AMessage>(kWhatFetchData, shared_from_this())->post(
                        10000); // 每10ms获取一次数据
                break;
            case kWhatRender:
                std::shared_ptr<VEFrame> frame;
                {
                    std::unique_lock<std::mutex> lock(m_Mutex);
                    if (!m_FrameQueue.empty()) {
                        frame = m_FrameQueue.front();
                        m_FrameQueue.pop_front();
                    }
                }
                if (frame && m_AudioRenderer) {
                    m_AudioRenderer->renderFrame(frame);
                }
                break;
        }
    }

    void VEAudioRender::fetchAudioData() {
        std::shared_ptr<VEFrame> frame;
        while (m_AudioDecoder->readFrame(frame) == 0) {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_FrameQueue.push_back(frame);
            m_Cond.notify_one();
        }
    }
}