#include "VEAudioSLESRender.h"
#include <iostream>
namespace VE {
    VEAudioSLESRender::VEAudioSLESRender() : engineObject(nullptr), engineEngine(nullptr),
                                             outputMixObject(nullptr), playerObject(nullptr),
                                             playerPlay(nullptr), bufferQueue(nullptr) {}

    VEAudioSLESRender::~VEAudioSLESRender() {
        uninit();
    }

    int VEAudioSLESRender::init() {
        return initOpenSLES();
    }

    int VEAudioSLESRender::configure(const AudioConfig &config) {
        // 配置音频渲染参数
        m_Config = config;
        return 0;
    }

    void VEAudioSLESRender::start() {
        if (playerPlay) {
            (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);
        }
    }

    void VEAudioSLESRender::stop() {
        if (playerPlay) {
            (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
        }
    }

    void VEAudioSLESRender::renderFrame(std::shared_ptr<VEFrame> frame) {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_FrameQueue.push_back(frame);
        m_Cond.notify_one();
    }

    int VEAudioSLESRender::uninit() {
        if (playerObject) {
            (*playerObject)->Destroy(playerObject);
            playerObject = nullptr;
            playerPlay = nullptr;
            bufferQueue = nullptr;
        }
        if (outputMixObject) {
            (*outputMixObject)->Destroy(outputMixObject);
            outputMixObject = nullptr;
        }
        if (engineObject) {
            (*engineObject)->Destroy(engineObject);
            engineObject = nullptr;
            engineEngine = nullptr;
        }
        return 0;
    }

    int VEAudioSLESRender::initOpenSLES() {
        // 创建引擎
        slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
        (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
        (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);

        // 创建输出混音器
        const SLInterfaceID ids[] = {SL_IID_VOLUME};
        const SLboolean req[] = {SL_BOOLEAN_FALSE};
        (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
        (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);

        // 创建播放器
        SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                           2};
        SLDataFormat_PCM format_pcm = {
                SL_DATAFORMAT_PCM, static_cast<SLuint32>(m_Config.channels),
                static_cast<SLuint32>(m_Config.sampleRate * 1000),
                static_cast<SLuint32>(m_Config.sampleFormat),
                static_cast<SLuint32>(m_Config.sampleFormat),
                SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, SL_BYTEORDER_LITTLEENDIAN
        };
        SLDataSource audioSrc = {&loc_bufq, &format_pcm};

        SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
        SLDataSink audioSnk = {&loc_outmix, nullptr};

        const SLInterfaceID ids1[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
        const SLboolean req1[] = {SL_BOOLEAN_TRUE};
        (*engineEngine)->CreateAudioPlayer(engineEngine, &playerObject, &audioSrc, &audioSnk, 1,
                                           ids1, req1);
        (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE);
        (*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playerPlay);
        (*playerObject)->GetInterface(playerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bufferQueue);

        // 注册回调
        (*bufferQueue)->RegisterCallback(bufferQueue, playerCallback, this);

        return 0;
    }

    void VEAudioSLESRender::playerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
        VEAudioSLESRender *audioRender = static_cast<VEAudioSLESRender *>(context);

        std::unique_lock<std::mutex> lock(audioRender->m_Mutex);
        if (audioRender->m_FrameQueue.empty()) {
            audioRender->m_Cond.wait(lock);
        }

        if (!audioRender->m_FrameQueue.empty()) {
            auto frame = audioRender->m_FrameQueue.front();
            audioRender->m_FrameQueue.pop_front();
            lock.unlock();

            // 将音频数据送入OpenSL ES缓冲区
            (*bq)->Enqueue(bq, frame->getFrame()->data[0], frame->getFrame()->linesize[0]);
        }
    }
}