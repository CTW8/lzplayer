#include "VEAudioRender.h"
#include "renders/VEAudioSLESRender.h"

namespace VE {
    namespace {
        /// 设备侧输出延迟估计(SLES 拿不到精确值)。入队的帧要等设备队列
        /// 前面的数据播完 + 器件延迟后才被听到，时钟不补偿会系统性超前，
        /// 表现为视频恒定领先音频几十毫秒。
        constexpr int64_t kDeviceOutputLatencyUs = 40000;
    }

    VEAudioRender::VEAudioRender(const std::shared_ptr<AMessage> &notify,const std::shared_ptr<VEAVsync> &avSync)
            :m_Notify(notify),m_AVSync(avSync) {
//        fp = fopen("/data/data/com.example.lzplayer/files/dump_audio.pcm","wb+");
//        if(fp == nullptr){
//            ALOGD("VEAudioRender:: /data/data/com.example.lzplayer/files/dump_audio.pcm open file failed!!!");
//        }else{
//            ALOGD("VEAudioRender:: /data/data/com.example.lzplayer/files/dump_audio.pcm open file success!!!");
//        }
    }

    VEAudioRender::~VEAudioRender() {
        if (m_AudioRenderer) {
            m_AudioRenderer->release();
        }
        if (fp) {
            fclose(fp);
        }
    }

    VEResult VEAudioRender::prepare(const VEAudioOutputConfig &config) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setInt32("samplerate", config.sampleRate);
        msg->setInt32("channel", config.channels);
        msg->setInt32("format", config.format);
        msg->post();
        return 0;
    }

    void VEAudioRender::queueFrame(const std::shared_ptr<VEFrame> &frame,
                                   const std::shared_ptr<AMessage> &consumedReply) {
        // 解码器线程调用：转投自己的 looper。队列代次在此刻盖章。
        auto msg = std::make_shared<AMessage>(kWhatQueueFrame, shared_from_this());
        msg->setObject("frame", frame);
        msg->setObject("reply", consumedReply);
        msg->setInt32("queueGen", mQueueGen.load());
        msg->post();
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

    VEResult VEAudioRender::seekTo(double_t timestamp) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp", timestamp);
        msg->post();
        return VE_OK;
    }

    VEResult VEAudioRender::flush() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioRender::pause() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioRender::release() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatRelease, shared_from_this());
        msg->post();
        return 0;
    }

    void VEAudioRender::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        switch (msg->what()) {
            case kWhatPrepare: {
                int sampleRate = 0, channel = 0, format = 0;
                msg->findInt32("samplerate", &sampleRate);
                msg->findInt32("channel", &channel);
                msg->findInt32("format", &format);

                m_AudioRenderer = std::make_shared<VEAudioSLESRender>();

                AudioConfig config;
                config.sampleRate = sampleRate;
                config.channels = channel;
                config.sampleFormat = format;
                // Obtain shared_ptr<AHandler> via shared_from_this(), cast to shared_ptr<VEAudioRender>,
                auto selfShared = std::dynamic_pointer_cast<VEAudioRender>(shared_from_this());
                auto wSelf = std::weak_ptr<VEAudioRender>(selfShared);
                // 该回调来自 OpenSL ES 的回调线程，只做投递；epoch 保证
                // seek/pause 之前排队的回调不会再消费新数据
                config.onCallback = [wSelf]() -> int {
                    if (auto self = wSelf.lock()) {
                        self->postRender();
                    }
                    return 0;
                };

                m_AudioRenderer->configure(config);
                break;
            }
            case kWhatStart: {
                if (m_AudioRenderer && !m_IsStarted) {
                    m_IsStarted = true;
                    postRender();
                    m_AudioRenderer->start();
                }
                break;
            }
            case kWhatStop: {
                m_IsStarted = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                if (m_AudioRenderer) {
                    m_AudioRenderer->stop();
                }
                postMessage(VE_NOTIFY_EVENT_STOP_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatPause:{
                m_IsStarted = false;
                ++m_Epoch;
                if (m_AudioRenderer) {
                    m_AudioRenderer->pause();
                }
                postMessage(VE_NOTIFY_EVENT_PAUSE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatFlush:{
                m_IsStarted = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                postMessage(VE_NOTIFY_EVENT_FLUSH_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatRender:{
                int32_t epoch = 0;
                msg->findInt32("epoch", &epoch);
                if (epoch != m_Epoch || !m_IsStarted) {
                    // seek/pause 之前投递的渲染消息，丢弃
                    break;
                }
                onRender();
                break;
            }
            case kWhatQueueFrame:{
                int32_t gen = 0;
                msg->findInt32("queueGen", &gen);
                if (gen != mQueueGen.load()) {
                    // flush/seek 之前在途的旧帧：丢弃。不回执——
                    // 解码器 flush 时已把 credit 清算归零
                    ALOGI("VEAudioRender stale queued frame gen=%d cur=%d",
                          gen, mQueueGen.load());
                    break;
                }
                std::shared_ptr<void> f, r;
                msg->findObject("frame", &f);
                msg->findObject("reply", &r);
                mFrames.emplace_back(std::static_pointer_cast<VEFrame>(f),
                                     std::static_pointer_cast<AMessage>(r));
                if (m_IsStarted && mFrames.size() == 1) {
                    // 队列从空转非空：喂帧链此前已停摆，帧到达即重新拉起
                    postRender(0);
                }
                break;
            }
            case kWhatSeek:{
                // 丢弃在途的渲染回调，并清掉设备缓冲里 seek 之前的 PCM
                m_IsStarted = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE,0,0,0, nullptr);
                break;
            }
            case kWhatRelease:{
                m_IsStarted = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                if (m_AudioRenderer) {
                    m_AudioRenderer->release();
                    // 置空，避免析构时二次 release
                    m_AudioRenderer.reset();
                }
                postMessage(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
                break;
            }
            default:{
                break;
            }
        }
    }

    VEResult VEAudioRender::onRender() {
        if (m_AudioRenderer == nullptr) {
            return VE_OK;
        }
        // 尽量把本地队列喂进设备(while 循环天然完成全深度预填)；
        // 设备满就停手，SLES 每播完一个缓冲会回调再驱动。
        // 队列空 = 喂帧链停摆：不再垫静音续命，下一帧到达(onQueueFrame)
        // 自动重启——数据到达就是唤醒。
        while (!mFrames.empty()) {
            std::shared_ptr<VEFrame> frame = mFrames.front().first;
            std::shared_ptr<AMessage> reply = mFrames.front().second;

            if (frame == nullptr) {
                mFrames.pop_front();
                if (reply) reply->post();
                continue;
            }

            if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
                ALOGI("VEAudioRender::%s - End of Stream (EOS) detected", __FUNCTION__);
                // 上报后停设备：不再有帧到达，链自然终结，
                // 播放完成后不存在任何空转(原静音保活的耗电问题结构性消失)
                postMessage(VE_NOTIFY_EVENT_EOS,0,0,0, nullptr);
                m_AudioRenderer->pause();
                m_IsStarted = false;
                mFrames.pop_front();
                if (reply) reply->post();
                return VE_EOS;
            }

            VEResult result = m_AudioRenderer->renderFrame(frame);
            if (result == VE_WOULD_BLOCK) {
                // 设备队列满：帧留在队首，等 SLES 回调腾出空位再来
                break;
            }
            mFrames.pop_front();
            if (reply) reply->post();
            if (result != VE_OK) {
                ALOGE("VEAudioRender failed: %d", result);
                return VE_UNKNOWN_ERROR;
            }
            ALOGV("VEAudioRender::%s - PTS: %lu", __FUNCTION__, frame->getPts());
            // 时钟按"正在被听到"的位置打点：入队 pts 减去设备队列里
            // 还没播完的数据时长和器件输出延迟
            const int64_t latencyUs =
                    m_AudioRenderer->getQueuedDurationUs() + kDeviceOutputLatencyUs;
            m_AVSync->updateAudioPts(
                    static_cast<double>(frame->getPts()) - static_cast<double>(latencyUs));
        }
        return VE_OK;
    }

    void VEAudioRender::postRender(int64_t delayUs) {
        auto msg = std::make_shared<AMessage>(kWhatRender, shared_from_this());
        msg->setInt32("epoch", m_Epoch);
        msg->post(delayUs);
    }

    VEResult VEAudioRender::postMessage(int32_t event, int32_t arg1, int32_t arg2, int64_t arg3,
                                        void *params) {
        std::shared_ptr<AMessage> msg = m_Notify->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_AUDIO_RENDER);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return 0;
    }
}