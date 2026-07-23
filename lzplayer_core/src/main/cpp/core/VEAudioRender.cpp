#include "VEAudioRender.h"
#include "renders/VEAudioSLESRender.h"

namespace VE {
    namespace {
        /// 静音帧也入队失败时的重试间隔
        constexpr int64_t kUnderrunRetryUs = 10000;
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
        if (mSliceBuffer) {
            free(mSliceBuffer);
            mSliceBuffer = nullptr;
        }
        if (fp) {
            fclose(fp);
        }
    }

    VEResult VEAudioRender::prepare(const std::shared_ptr<IMediaDecoder> &decoder,
                                    const VEAudioOutputConfig &config) {
        if (decoder == nullptr) {
            return VE_INVALID_PARAMS;
        }

        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setInt32("samplerate", config.sampleRate);
        msg->setInt32("channel", config.channels);
        msg->setInt32("format", config.format);
        msg->setObject("decode", decoder);
        msg->post();
        return 0;
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

                std::shared_ptr<void> tmp = nullptr;
                msg->findObject("decode", &tmp);
                m_AudioDecoder = std::static_pointer_cast<IMediaDecoder>(tmp);

                m_AudioRenderer = std::make_shared<VEAudioSLESRender>();////请修复

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

                mSliceBuffer = (uint8_t *)malloc(1024 * 2);
                memset(mSliceBuffer,0,1024*2);
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
                m_PendingFrame.reset();
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
                m_PendingFrame.reset();
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
            case kWhatSeek:{
                // 丢弃在途的渲染回调，并清掉设备缓冲里 seek 之前的 PCM
                m_IsStarted = false;
                ++m_Epoch;
                m_PendingFrame.reset();
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE,0,0,0, nullptr);
                break;
            }
            case kWhatRelease:{
                m_IsStarted = false;
                ++m_Epoch;
                m_PendingFrame.reset();
                if (m_AudioRenderer) {
                    m_AudioRenderer->release();
                    // 置空，避免析构时二次 release
                    m_AudioRenderer.reset();
                }
                m_AudioDecoder.reset();
                postMessage(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
                break;
            }
            default:{
                break;
            }
        }
    }

    VEResult VEAudioRender::onRender() {
        if (m_AudioRenderer != nullptr) {
            std::shared_ptr<VEFrame> frame = nullptr;
            if (m_PendingFrame != nullptr) {
                // 上次因设备队列满被打回的帧：优先重试，不从解码器取新帧
                frame = m_PendingFrame;
            } else {
                VEResult ret = m_AudioDecoder->readFrame(frame);
                ALOGV("VEAudioRender::%s enter#1", __FUNCTION__);
                if (ret == VE_NOT_ENOUGH_DATA) {
                    ALOGV("VEAudioRender::%s - underrun, feeding silence", __FUNCTION__);
                    // 用静音帧维持 SLES 的回调链：回调是整个音频渲染的唯一驱动源，
                    // 一旦断开就再也不会有回调来消费后续数据。
                    frame = std::make_shared<VEFrame>();
                    if (m_AudioRenderer->renderFrame(frame) != VE_OK) {
                        // 静音帧也入队失败(队列满或回调链已断)，改由延时消息自行重启
                        ALOGW("VEAudioRender::%s - silence enqueue failed, retry later", __FUNCTION__);
                        postRender(kUnderrunRetryUs);
                    }
                    return VE_NOT_ENOUGH_DATA;
                }
            }
            ALOGV("VEAudioRender::%s enter#2", __FUNCTION__);
            if (frame != nullptr) {
                if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
                    ALOGI("VEAudioRender::%s - End of Stream (EOS) detected", __FUNCTION__);
                    // 只上报，不越权命令解码器：渲染器 pause 解码器会产生一条
                    // VEPlayer 不知情的 PAUSE_DONE 回执污染命令协议。到达 EOS 的
                    // 解码器本来就已自停(mIsEOS)，收敛动作统一归 VEPlayer。
                    postMessage(VE_NOTIFY_EVENT_EOS,0,0,0, nullptr);
                    return VE_EOS;
                }

//                if(fp != nullptr){
//
//                    const int channels         = frame->getFrame()->ch_layout.nb_channels;
//                    const AVSampleFormat fmt   = static_cast<AVSampleFormat>(frame->getFrame()->format);
//                    const int planar           = av_sample_fmt_is_planar(fmt);
//                    const int bytes_per_sample = av_get_bytes_per_sample(fmt);
//
//                    if(planar == 0){
//                        const size_t data_size = static_cast<size_t>(frame->getFrame()->nb_samples) * channels * bytes_per_sample;
//                        ALOGD("VEAudioRender data_size:%zu  linesize:%d",data_size,frame->getFrame()->linesize[0]);
//                        fwrite(frame->getFrame()->data[0],data_size,1,fp);
//                        fflush(fp);
//                    }
//                }


                VEResult result = m_AudioRenderer->renderFrame(frame);
                if (result == VE_WOULD_BLOCK) {
                    // 设备队列满(典型是 seek 后刚预填过静音)：留住这一帧延时重试。
                    // 不推进时钟——这帧还没被播放。
                    m_PendingFrame = frame;
                    postRender(kUnderrunRetryUs);
                    return VE_WOULD_BLOCK;
                }
                m_PendingFrame.reset();
                if (result != VE_OK) {
                    ALOGE("VEAudioRender failed: %d", result);
                    return VE_UNKNOWN_ERROR;
                }
                ALOGV("VEAudioRender::%s - PTS: %lu exit", __FUNCTION__, frame->getPts());
                // 时钟按"正在被听到"的位置打点：入队 pts 减去设备队列里
                // 还没播完的数据时长和器件输出延迟
                const int64_t latencyUs =
                        m_AudioRenderer->getQueuedDurationUs() + kDeviceOutputLatencyUs;
                m_AVSync->updateAudioPts(
                        static_cast<double>(frame->getPts()) - static_cast<double>(latencyUs));
            } else {
                ALOGD("VEAudioRender frame is null");
            }
        }
        return 0;
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