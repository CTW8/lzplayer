#include "VEAudioRender.h"
#include "renders/VEAudioSLESRender.h"
#include "renders/VEAAudioRender.h"

namespace VE {
    namespace {
        /// 设备侧输出延迟估计(SLES 拿不到精确值)。入队的帧要等设备队列
        /// 前面的数据播完 + 器件延迟后才被听到，时钟不补偿会系统性超前，
        /// 表现为视频恒定领先音频几十毫秒。
        constexpr int64_t kDeviceOutputLatencyUs = 40000;
        /// mFrames 深度兜底。正常由解码器 credit(=AUDIO_FRAME_QUEUE_SIZE)封顶，
        /// 这里只防异常多发回执导致的无界增长：超阈值丢最旧帧并照常回执还 credit。
        constexpr size_t kMaxFramesBackstop = 100;
        /// 变速输出切块粒度，与 SLES 缓冲时长一致
        constexpr int kDeviceBlockMs = 20;
    }

    VEAudioRender::VEAudioRender(const std::shared_ptr<AMessage> &notify,const std::shared_ptr<VEAVsync> &avSync)
            :m_Notify(notify),m_AVSync(avSync) {
    }

    VEAudioRender::~VEAudioRender() {
        if (m_AudioRenderer) {
            m_AudioRenderer->release();
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

    VEResult VEAudioRender::seekTo(double timestamp) {
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

                m_OutSampleRate = sampleRate;
                m_OutChannels = channel;
                m_OutFormat = format;
                // 设备块粒度与 SLES 缓冲对齐(20ms)，变速输出按它切块
                m_BlockSamples = sampleRate * kDeviceBlockMs / 1000;
                mSonic.configure(sampleRate, channel);
                mSonic.setSpeed(m_Speed);

                // 后端按系统版本选：AAudio(API 26+) 延迟与功耗更好，
                // 且能拿到真实呈现位置(音频时钟精度的根本改善)；
                // 低版本退回 SLES。两者都实现 IAudioRender，上层无感。
                if (!m_ForceSles && VEAAudioRender::isAvailable()) {
                    m_AudioRenderer = std::make_shared<VEAAudioRender>();
                    m_BackendName = "AAudio";
                    ALOGI("VEAudioRender::%s using AAudio backend", __FUNCTION__);
                } else {
                    m_AudioRenderer = std::make_shared<VEAudioSLESRender>();
                    m_BackendName = "OpenSL ES";
                    ALOGI("VEAudioRender::%s using OpenSL ES backend%s", __FUNCTION__,
                          m_ForceSles ? " (forced)" : "");
                }

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

                if (m_AudioRenderer->configure(config) != VE_OK) {
                    // AAudio 建流失败(厂商实现有坑/设备占用)：退回 SLES 再试一次，
                    // 不能因为一个后端不可用就让整个播放失败
                    ALOGW("VEAudioRender::%s backend configure failed, falling back to SLES",
                          __FUNCTION__);
                    m_AudioRenderer = std::make_shared<VEAudioSLESRender>();
                    m_BackendName = "OpenSL ES";
                    if (m_AudioRenderer->configure(config) != VE_OK) {
                        ALOGE("VEAudioRender::%s no usable audio backend", __FUNCTION__);
                        postMessage(VE_NOTIFY_EVENT_ERROR, VE_UNKNOWN_ERROR, 0, 0, nullptr);
                    }
                }
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
                m_DeviceFailed = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                resetStretchState();
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
                m_DeviceFailed = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                resetStretchState();
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
                // 深度兜底：异常多发回执下 credit 单一防线失守时，丢最旧帧
                // 并照常回执还 credit，避免 mFrames 无界增长耗尽内存
                if (mFrames.size() >= kMaxFramesBackstop) {
                    ALOGW("VEAudioRender::onQueueFrame frames overflow %zu, drop oldest",
                          mFrames.size());
                    if (mFrames.front().second) {
                        mFrames.front().second->post();
                    }
                    mFrames.pop_front();
                }
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
                m_DeviceFailed = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                resetStretchState();
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE,0,0,0, nullptr);
                break;
            }
            case kWhatSetSpeed: {
                float speed = 1.0f;
                double anchorPtsUs = 0;
                msg->findFloat("speed", &speed);
                msg->findDouble("anchorPtsUs", &anchorPtsUs);
                if (speed == m_Speed) {
                    break;
                }
                ALOGI("VEAudioRender::%s speed %.2f -> %.2f", __FUNCTION__, m_Speed, speed);
                m_Speed = speed;
                mSonic.setSpeed(speed);
                // 设备缓冲里还压着旧速率的 PCM，不清掉会在切换点听到
                // 一段错速的音；清完从当前播放位置重新锚定记账
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                resetStretchState();
                m_AnchorPtsUs = static_cast<int64_t>(anchorPtsUs);
                if (m_IsStarted) {
                    postRender(0);
                }
                break;
            }
            case kWhatRelease:{
                m_IsStarted = false;
                m_DeviceFailed = false;
                ++m_Epoch;
                mQueueGen++;
                mFrames.clear();
                resetStretchState();
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
        // 设备侧渲染出错后不再继续喂：错误已上报，等播放器收敛
        if (m_DeviceFailed) {
            return VE_UNKNOWN_ERROR;
        }
        // 1.0x 时完全旁路变速器，走原来的直通路径，零额外开销
        if (m_Speed == 1.0f || !mSonic.isConfigured()) {
            return renderPassthrough();
        }
        return renderTimeStretched();
    }

    VEResult VEAudioRender::renderPassthrough() {
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
                // 设备侧硬错误：此刻喂帧链已经断了(设备不会再回调，队列非空
                // 也不会再被"空转非空"重新踢起来)。必须上报交播放器收敛，
                // 否则音频永久停摆而上层毫无感知。
                ALOGE("VEAudioRender device render failed: %d, reporting error", result);
                m_IsStarted = false;
                m_DeviceFailed = true;
                postMessage(VE_NOTIFY_EVENT_ERROR, result, 0, 0, nullptr);
                return VE_UNKNOWN_ERROR;
            }
            ALOGF("VEAudioRender::%s - PTS: %" PRId64, __FUNCTION__, frame->getPts());
            // 时钟按"正在被听到"的位置打点：入队 pts 减去设备队列里
            // 还没播完的数据时长和器件输出延迟
            anchorClock(static_cast<double>(frame->getPts()));
        }
        return VE_OK;
    }

    // ---------------------------------------------------------------------
    // 变速路径：帧 → sonic → 按设备块喂出去
    // ---------------------------------------------------------------------

    void VEAudioRender::anchorClock(double mediaPosUs) {
        if (!m_AVSync) {
            return;
        }
        // T8：首个音频帧已进设备、时钟首次起锚。纯音频文件没有 T6/T7，
        // 启播总耗时会退化成以这个点为准(见 VEStartupTrace::toJson)
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T8_FIRST_AUDIO);
        }
        // 设备里还没播出去的数据，按实时时长计；换算成媒体时长要乘速率
        // (2 倍速下 100ms 的设备数据对应 200ms 的媒体内容)
        const double queuedRealUs =
                static_cast<double>(m_AudioRenderer->getQueuedDurationUs() +
                                    kDeviceOutputLatencyUs);
        m_AVSync->updateAudioPts(mediaPosUs - queuedRealUs * m_Speed);
    }

    void VEAudioRender::resetStretchState() {
        mSonic.flush();
        m_AnchorPtsUs = kNoAnchor;
        m_OutSamplesTotal = 0;
        m_PendingOut.reset();
        m_Draining = false;
    }

    std::shared_ptr<VEFrame> VEAudioRender::makeStretchedFrame(int samplesPerChannel) {
        auto out = std::make_shared<VEFrame>(m_OutSampleRate, m_OutChannels,
                                             samplesPerChannel, m_OutFormat);
        AVFrame *av = out->getFrame();
        if (av == nullptr || av->data[0] == nullptr) {
            ALOGE("VEAudioRender::%s alloc stretched frame failed", __FUNCTION__);
            return nullptr;
        }
        const size_t bytes = static_cast<size_t>(samplesPerChannel) * m_OutChannels *
                             av_get_bytes_per_sample(static_cast<AVSampleFormat>(m_OutFormat));
        memcpy(av->data[0], mSonic.stagingData(), bytes);
        av->nb_samples = samplesPerChannel;
        out->setFrameType(E_FRAME_TYPE_AUDIO);
        return out;
    }

    VEResult VEAudioRender::renderTimeStretched() {
        while (true) {
            // ① 设备满时留下的那块先补喂
            if (m_PendingOut != nullptr) {
                const VEResult r = m_AudioRenderer->renderFrame(m_PendingOut);
                if (r == VE_WOULD_BLOCK) {
                    return VE_OK;   // 设备还满着，等 SLES 回调再来
                }
                if (r != VE_OK) {
                    ALOGE("VEAudioRender device render failed: %d, reporting error", r);
                    m_IsStarted = false;
                    m_DeviceFailed = true;
                    postMessage(VE_NOTIFY_EVENT_ERROR, r, 0, 0, nullptr);
                    return VE_UNKNOWN_ERROR;
                }
                m_OutSamplesTotal += m_PendingOut->getFrame()->nb_samples;
                m_PendingOut.reset();
                // 已送出数据末端的媒体位置 = 锚点 + 输出样本对应的媒体时长。
                // 变速后 N 个输出样本对应 N*speed 个媒体样本的内容。
                if (m_AnchorPtsUs != kNoAnchor && m_OutSampleRate > 0) {
                    const double advancedUs =
                            static_cast<double>(m_OutSamplesTotal) * 1000000.0 /
                            m_OutSampleRate * m_Speed;
                    anchorClock(static_cast<double>(m_AnchorPtsUs) + advancedUs);
                }
                continue;
            }

            // ② sonic 里攒够一个设备块就取出来喂
            if (mSonic.samplesAvailable() >= m_BlockSamples) {
                const int got = mSonic.read(m_BlockSamples);
                if (got > 0) {
                    m_PendingOut = makeStretchedFrame(got);
                    if (m_PendingOut == nullptr) {
                        return VE_UNKNOWN_ERROR;
                    }
                    continue;
                }
            }

            // ③ 输入耗尽后的收尾：把 sonic 尾巴冲出来
            if (m_Draining) {
                const int left = mSonic.samplesAvailable();
                if (left > 0) {
                    const int got = mSonic.read(left);
                    if (got > 0) {
                        m_PendingOut = makeStretchedFrame(got);
                        continue;
                    }
                }
                // 尾巴也放完了，此刻才算真正播完
                ALOGI("VEAudioRender::%s - EOS after drain", __FUNCTION__);
                postMessage(VE_NOTIFY_EVENT_EOS, 0, 0, 0, nullptr);
                m_AudioRenderer->pause();
                m_IsStarted = false;
                m_Draining = false;
                return VE_EOS;
            }

            // ④ 吞下一帧输入
            if (mFrames.empty()) {
                return VE_OK;   // 喂帧链停摆，等新帧到达重新拉起
            }
            std::shared_ptr<VEFrame> frame = mFrames.front().first;
            std::shared_ptr<AMessage> reply = mFrames.front().second;
            mFrames.pop_front();

            if (frame == nullptr) {
                if (reply) reply->post();
                continue;
            }
            if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
                // 输入到头：让 sonic 把滞留样本吐出来，别丢尾音
                mSonic.drain();
                m_Draining = true;
                if (reply) reply->post();
                continue;
            }

            AVFrame *av = frame->getFrame();
            if (av == nullptr || av->data[0] == nullptr || av->nb_samples <= 0) {
                if (reply) reply->post();
                continue;
            }
            if (m_AnchorPtsUs == kNoAnchor) {
                // 本段的记账起点：之后位置全靠输出样本数推算，
                // 不再逐帧读 pts(变速后输入帧与输出块不再一一对应)
                m_AnchorPtsUs = frame->getPts();
                m_OutSamplesTotal = 0;
            }
            mSonic.write(av->data[0], av->nb_samples);
            // credit 在"帧被吞进变速器"时归还——数据已完成交接，
            // 解码器可以继续供给，不必等它真正播出去
            if (reply) reply->post();
        }
    }

    VEResult VEAudioRender::setSpeed(float speed, double anchorPtsUs) {
        auto msg = std::make_shared<AMessage>(kWhatSetSpeed, shared_from_this());
        msg->setFloat("speed", speed);
        msg->setDouble("anchorPtsUs", anchorPtsUs);
        msg->post();
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