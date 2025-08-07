#include "VEAudioRender.h"
#include "AudioOpenSLESOutput.h"
#include "renders/VEAudioSLESRender.h"

namespace VE {
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

    VEResult VEAudioRender::prepare(VEBundle params) {
        if(!params.contains("samplerate") || !params.contains("channel") || !params.contains("format")){
            return VE_INVALID_PARAMS;
        }

        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPrepare, shared_from_this());
        msg->setInt32("samplerate",params.get<int>("samplerate"));
        msg->setInt32("channel",params.get<int>("channel"));
        msg->setInt32("format",params.get<int>("format"));
        msg->setObject("decode",params.get<std::shared_ptr<VEAudioDecoder>>("decode"));
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
        msg->post();
        return 0;
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
                m_AudioDecoder = std::static_pointer_cast<VEAudioDecoder>(tmp);

                m_AudioRenderer = std::make_shared<VEAudioSLESRender>();////请修复

                AudioConfig config;
                config.sampleRate = sampleRate;
                config.channels = channel;
                config.sampleFormat = format;
                // Obtain shared_ptr<AHandler> via shared_from_this(), cast to shared_ptr<VEAudioRender>,
                auto selfShared = std::dynamic_pointer_cast<VEAudioRender>(shared_from_this());
                auto wSelf = std::weak_ptr<VEAudioRender>(selfShared);
                config.onCallback = [wSelf]() -> int {
                    if (auto self = wSelf.lock()) {
                        std::make_shared<AMessage>(VEAudioRender::kWhatRender, self)->post();
                    }
                    return 0;
                };

                m_AudioRenderer->configure(config);

                mSliceBuffer = (uint8_t *)malloc(1024 * 2);
                memset(mSliceBuffer,0,1024*2);
                break;
            }
            case kWhatStart: {
                if (m_AudioRenderer) {
                    std::make_shared<AMessage>(kWhatRender,shared_from_this())->post();
                    m_AudioRenderer->start();
                }
                break;
            }
            case kWhatStop: {
                if (m_AudioRenderer) {
                    m_AudioRenderer->stop();
                }
                break;
            }
            case kWhatPause:{
                if (m_AudioRenderer) {
                    m_AudioRenderer->pause();
                }
                break;
            }
            case kWhatFlush:{
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                break;
            }
            case kWhatRender:{
                onRender();
                break;
            }
            case kWhatSeek:{
                if (m_AudioRenderer) {
                    m_AudioRenderer->flush();
                }
                break;
            }
            case kWhatRelease:{
                if (m_AudioRenderer) {
                    m_AudioRenderer->release();
                }
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
            VEResult ret = m_AudioDecoder->readFrame(frame);
            ALOGI("VEAudioRender::%s enter#1", __FUNCTION__);
            if (ret == VE_NOT_ENOUGH_DATA) {
                ALOGI("VEAudioRender::%s - needMoreFrame", __FUNCTION__);
                frame = std::make_shared<VEFrame>();
                VEResult result = m_AudioRenderer->renderFrame(frame);
                if (result != SL_RESULT_SUCCESS) {
                    ALOGE("VEAudioRender failed: %d", result);
                    return VE_UNKNOWN_ERROR;
                }
                return VE_NOT_ENOUGH_DATA;
            }
            ALOGI("VEAudioRender::%s enter#2", __FUNCTION__);
            if (frame != nullptr) {
                if (frame->getFrameType() == E_FRAME_TYPE_EOF) {
                    ALOGI("VEAudioRender::%s - End of Stream (EOS) detected", __FUNCTION__);
                    std::shared_ptr<AMessage> eosMsg = m_Notify->dup();
                    eosMsg->setInt32("type", kWhatEOS);
                    eosMsg->post();

                    m_AudioDecoder->pause();

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
                if (result != SL_RESULT_SUCCESS) {
                    ALOGE("VEAudioRender failed: %d", result);
                    return VE_UNKNOWN_ERROR;
                }
                ALOGI("VEAudioRender::%s - PTS: %lu exit", __FUNCTION__, frame->getPts());
                m_AVSync->updateAudioPts(frame->getPts());
            } else {
                ALOGD("VEAudioRender frame is null");
            }
        }
        return 0;
    }
}