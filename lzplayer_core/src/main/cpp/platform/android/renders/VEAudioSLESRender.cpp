#include "VEAudioSLESRender.h"
#include <iostream>
namespace VE {

    void slesBufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
        // 通知onPlay该向opensles送数据
        VEAudioSLESRender * pThis = (VEAudioSLESRender*)context;
        if(pThis){
            ALOGV("===>VEAudioSLESRender bufferQueueCallback notify play!!!");
            {
                // 队首缓冲已播完，释放对应的帧引用(静音占位为 nullptr)
                std::lock_guard<std::mutex> lock(pThis->m_Mutex);
                if (!pThis->m_FrameQueue.empty()) {
                    pThis->m_FrameQueue.pop_front();
                }
                if (!pThis->m_QueuedDurationsUs.empty()) {
                    pThis->m_QueuedTotalUs -= pThis->m_QueuedDurationsUs.front();
                    if (pThis->m_QueuedTotalUs < 0) pThis->m_QueuedTotalUs = 0;
                    pThis->m_QueuedDurationsUs.pop_front();
                }
            }
            pThis->m_AudioCallback();
        }
    }

    VEAudioSLESRender::VEAudioSLESRender()
            : mEngineObject(nullptr)
            , mEngineEngine(nullptr)
            , mOutputMixObject(nullptr)
            , mPlayerObject(nullptr)
            , mPlayerPlay(nullptr)
            , mBufferQueue(nullptr)
            , m_IsConfigured(false)
            , m_IsPlaying(false)
            , m_BufferSize(0)
            , m_TempBuffer(nullptr) {
    }

    VEAudioSLESRender::~VEAudioSLESRender(){
        release();
    }

    VEResult VEAudioSLESRender::configure(const AudioConfig &config) {

        if (m_IsConfigured) {
            ALOGI("VEAudioSLESRender Already configured, release first");
            release();
        }

        // 保存配置
        m_Config = config;

        // 创建并初始化 OpenSL ES 引擎
        SLresult result = slCreateEngine(&mEngineObject, 0, NULL, 0, NULL, NULL);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to create engine: %d", result);
            return VE_UNKNOWN_ERROR;
        }

        result = (*mEngineObject)->Realize(mEngineObject, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to realize engine: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        result = (*mEngineObject)->GetInterface(mEngineObject, SL_IID_ENGINE, &mEngineEngine);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to get engine interface: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        // 创建输出混音器
        result = (*mEngineEngine)->CreateOutputMix(mEngineEngine, &mOutputMixObject, 0, nullptr, nullptr);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to create output mix: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        result = (*mOutputMixObject)->Realize(mOutputMixObject, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to realize output mix: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        // 根据配置设置音频格式
        SLuint32 sampleRate = convertSampleRate(config.sampleRate);
        SLuint32 channels = config.channels;
        SLuint32 channelMask = (channels == 1) ? SL_SPEAKER_FRONT_CENTER :
                               (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);

        // 配置音频源
        SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
                SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                BUFFER_QUEUE_SIZE
        };

        SLDataFormat_PCM format_pcm = {
                SL_DATAFORMAT_PCM,
                channels,
                sampleRate,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                channelMask,
                SL_BYTEORDER_LITTLEENDIAN
        };
        SLDataSource audioSrc = {&loc_bufq, &format_pcm};

        // 配置音频接收器
        SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, mOutputMixObject};
        SLDataSink audioSnk = {&loc_outmix, NULL};

        // 创建音频播放器
        const SLInterfaceID ids[2] = {SL_IID_PLAY, SL_IID_BUFFERQUEUE};
        const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

        result = (*mEngineEngine)->CreateAudioPlayer(mEngineEngine, &mPlayerObject, &audioSrc,
                                                     &audioSnk, 2, ids, req);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to create audio player: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        result = (*mPlayerObject)->Realize(mPlayerObject, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to realize audio player: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        result = (*mPlayerObject)->GetInterface(mPlayerObject, SL_IID_PLAY, &mPlayerPlay);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to get play interface: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        result = (*mPlayerObject)->GetInterface(mPlayerObject, SL_IID_BUFFERQUEUE, &mBufferQueue);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to get buffer queue interface: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        // 注册回调函数
        result = (*mBufferQueue)->RegisterCallback(mBufferQueue, slesBufferQueueCallback, this);
        if (result != SL_RESULT_SUCCESS) {
            ALOGE("VEAudioSLESRender Failed to register buffer queue callback: %d", result);
            cleanup();
            return VE_UNKNOWN_ERROR;
        }

        m_AudioCallback = config.onCallback;

        // 计算缓冲区大小（以毫秒为单位）
        m_BufferSize = calculateBufferSize(config.sampleRate, config.channels, BUFFER_DURATION_MS);

        // 分配临时缓冲区
        m_TempBuffer = (uint8_t*)malloc(m_BufferSize);
        if (!m_TempBuffer) {
            ALOGE("VEAudioSLESRender Failed to allocate temp buffer");
            cleanup();
            return VE_UNKNOWN_ERROR;
        }
        memset(m_TempBuffer, 0, m_BufferSize);

        m_IsConfigured = true;
        ALOGI("VEAudioSLESRender configured successfully");
        return VE_OK;
    }

    VEResult VEAudioSLESRender::start() {

        if (!m_IsConfigured) {
            ALOGE("VEAudioSLESRender Not configured yet");
            return VE_UNKNOWN_ERROR;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);

        // 重复 start 是幂等的，不是错误
        if (m_IsPlaying) {
            ALOGI("VEAudioSLESRender Already playing");
            return VE_OK;
        }

        if (mPlayerPlay != NULL) {
            SLresult result = (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_PLAYING);
            if (result != SL_RESULT_SUCCESS) {
                ALOGE("VEAudioSLESRender Failed to set play state: %d", result);
                return VE_UNKNOWN_ERROR;
            }
            m_IsPlaying = true;
        }

        ALOGI("VEAudioSLESRender started successfully");
        return VE_OK;
    }

    VEResult VEAudioSLESRender::pause() {

        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_IsPlaying) {
            ALOGI("VEAudioSLESRender Not playing");
            return VE_OK;
        }

        if (mPlayerPlay != NULL) {
            SLresult result = (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_PAUSED);
            if (result != SL_RESULT_SUCCESS) {
                ALOGE("VEAudioSLESRender Failed to set pause state: %d", result);
                return VE_UNKNOWN_ERROR;
            }
            m_IsPlaying = false;
        }

        ALOGI("VEAudioSLESRender paused successfully");
        return VE_OK;
    }

    VEResult VEAudioSLESRender::flush() {

        std::lock_guard<std::mutex> lock(m_Mutex);

        // 先清设备队列再释放帧引用：设备可能还握着在途缓冲的指针
        if (mBufferQueue != NULL) {
            (*mBufferQueue)->Clear(mBufferQueue);
        }
        m_FrameQueue.clear();
        m_QueuedDurationsUs.clear();
        m_QueuedTotalUs = 0;

        if (mBufferQueue != NULL) {
            // 只预填 1 个静音缓冲：够在恢复播放时拉起回调链，同时留出
            // 空位给 seek 后的第一帧真实数据——填满的话首帧必然入队失败。
            if ((*mBufferQueue)->Enqueue(mBufferQueue, m_TempBuffer, m_BufferSize)
                    == SL_RESULT_SUCCESS) {
                m_FrameQueue.push_back(nullptr);
                m_QueuedDurationsUs.push_back(BUFFER_DURATION_MS * 1000);
                m_QueuedTotalUs += BUFFER_DURATION_MS * 1000;
            }
        }

        ALOGI("VEAudioSLESRender flushed successfully");
        return VE_OK;
    }

    VEResult VEAudioSLESRender::stop() {

        std::lock_guard<std::mutex> lock(m_Mutex);

        if (mPlayerPlay != NULL) {
            (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_STOPPED);
            m_IsPlaying = false;
        }

        // 释放帧引用前必须先清掉设备队列，否则设备还握着这些缓冲的指针
        if (mBufferQueue != NULL) {
            (*mBufferQueue)->Clear(mBufferQueue);
        }
        m_FrameQueue.clear();
        m_QueuedDurationsUs.clear();
        m_QueuedTotalUs = 0;

        ALOGI("VEAudioSLESRender stopped successfully");
        return VE_OK;
    }

    VEResult VEAudioSLESRender::renderFrame(std::shared_ptr<VEFrame> frame) {
        if (!frame || !m_IsConfigured) {
            return VE_UNKNOWN_ERROR;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);

        if (frame && frame->getFrame() && frame->getFrame()->data[0]) {
            const int channels         = frame->getFrame()->ch_layout.nb_channels;
            const AVSampleFormat fmt   = static_cast<AVSampleFormat>(frame->getFrame()->format);
            const int bytes_per_sample = av_get_bytes_per_sample(fmt);

            const size_t data_size = static_cast<size_t>(frame->getFrame()->nb_samples) * channels * bytes_per_sample;

            // 入队音频数据。SLES 不拷贝数据，缓冲要到播放完成回调才失效，
            // 必须持有帧引用直到该缓冲被消费(回调中按 FIFO 弹出)。
            SLresult result = (*mBufferQueue)->Enqueue(mBufferQueue,
                                                       frame->getFrame()->data[0],
                                                       data_size);
            if (result != SL_RESULT_SUCCESS) {
                // 典型场景是设备队列满(如 seek 后刚预填过静音)。如实上报，
                // 由调用方留住这一帧稍后重试；这里不能垫静音顶掉空位，
                // 否则这帧数据被静默丢弃、时钟还会被没播的帧推进。
                ALOGW("VEAudioSLESRender enqueue would block: %d", result);
                return VE_WOULD_BLOCK;
            }
            m_FrameQueue.push_back(frame);
            const int64_t durUs = (m_Config.sampleRate > 0)
                    ? static_cast<int64_t>(frame->getFrame()->nb_samples) * 1000000 /
                      m_Config.sampleRate
                    : 0;
            m_QueuedDurationsUs.push_back(durUs);
            m_QueuedTotalUs += durUs;
        } else {
            // 提供静音数据(m_TempBuffer 生命周期由本对象管理，用 nullptr 占位对齐 FIFO)
            if ((*mBufferQueue)->Enqueue(mBufferQueue, m_TempBuffer, m_BufferSize)
                    != SL_RESULT_SUCCESS) {
                return VE_WOULD_BLOCK;
            }
            m_FrameQueue.push_back(nullptr);
            m_QueuedDurationsUs.push_back(BUFFER_DURATION_MS * 1000);
            m_QueuedTotalUs += BUFFER_DURATION_MS * 1000;
        }

        return VE_OK;
    }

    VEResult VEAudioSLESRender::release() {

        // 停止播放
        if (m_IsPlaying) {
            stop();
        }

        cleanup();

        // 释放临时缓冲区
        if (m_TempBuffer) {
            free(m_TempBuffer);
            m_TempBuffer = nullptr;
        }

        // 清空帧队列
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_FrameQueue.clear();
            m_QueuedDurationsUs.clear();
            m_QueuedTotalUs = 0;
        }

        m_IsConfigured = false;
        m_IsPlaying = false;

        ALOGI("VEAudioSLESRender released successfully");
        return VE_OK;
    }

    void VEAudioSLESRender::cleanup() {
        if (mPlayerObject != NULL) {
            (*mPlayerObject)->Destroy(mPlayerObject);
            mPlayerObject = NULL;
            mPlayerPlay = NULL;
            mBufferQueue = NULL;
        }

        if (mOutputMixObject != NULL) {
            (*mOutputMixObject)->Destroy(mOutputMixObject);
            mOutputMixObject = NULL;
        }

        if (mEngineObject != NULL) {
            (*mEngineObject)->Destroy(mEngineObject);
            mEngineObject = NULL;
            mEngineEngine = NULL;
        }
    }

    int64_t VEAudioSLESRender::getQueuedDurationUs() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // 按各块真实样本数累加，而不是块数 × 固定时长
        return m_QueuedTotalUs;
    }

    SLuint32 VEAudioSLESRender::convertSampleRate(int sampleRate) {
        switch (sampleRate) {
            case 8000:   return SL_SAMPLINGRATE_8;
            case 11025:  return SL_SAMPLINGRATE_11_025;
            case 16000:  return SL_SAMPLINGRATE_16;
            case 22050:  return SL_SAMPLINGRATE_22_05;
            case 24000:  return SL_SAMPLINGRATE_24;
            case 32000:  return SL_SAMPLINGRATE_32;
            case 44100:  return SL_SAMPLINGRATE_44_1;
            case 48000:  return SL_SAMPLINGRATE_48;
            case 64000:  return SL_SAMPLINGRATE_64;
            case 88200:  return SL_SAMPLINGRATE_88_2;
            case 96000:  return SL_SAMPLINGRATE_96;
            case 192000: return SL_SAMPLINGRATE_192;
            default:     return SL_SAMPLINGRATE_44_1;  // 默认44.1kHz
        }
    }

    size_t VEAudioSLESRender::calculateBufferSize(int sampleRate, int channels, int durationMs) {
        // 计算指定时长的缓冲区大小（16位PCM）
        return (sampleRate * channels * 2 * durationMs) / 1000;
    }
}