//
// Created by 李振 on 2024/6/30.
//

#include "AudioOpenSLESOutput.h"

#define SL_CHECK_ERROR(result, msg) \
    if (result != SL_RESULT_SUCCESS) { \
        LOGE("AudioOpenSLESOutput","%s failed: %d", msg, result); \
        return; \
    }
#define AUDIO_OUTPUT_FRAMES_SIZE    (1024*4)

void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    // 通知onPlay该向opensles送数据
    AudioOpenSLESOutput * pThis = (AudioOpenSLESOutput*)context;
    if(pThis){
        std::lock_guard<std::mutex> lk(pThis->mMutex);
        pThis->mCond.notify_one();
    }
}

status_t AudioOpenSLESOutput::init(std::shared_ptr<VEAudioDecoder> decoder,int samplerate, int channel, int format) {
//    fp = fopen("/data/local/tmp/dump_arender.pcm","wb+");
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit,shared_from_this());
    msg->setInt32("samplerate",samplerate);
    msg->setInt32("channel",channel);
    msg->setInt32("foramt",format);
    msg->setObject("decoder",decoder);
    msg->post();
    return 0;
}

void AudioOpenSLESOutput::start() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart,shared_from_this());
    msg->post();
}

void AudioOpenSLESOutput::stop() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop,shared_from_this());
    msg->post();
}

void AudioOpenSLESOutput::unInit() {
//    if(fp){
//        fflush(fp);
//        fclose(fp);
//    }
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit,shared_from_this());
    msg->post();
}

void AudioOpenSLESOutput::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatInit:{
            int sampleRate = 0;
            int channel = 0;
            int format =0;

            std::shared_ptr<void> tmp;
            msg->findObject("decoder",&tmp);
            mAudioDecoder = std::static_pointer_cast<VEAudioDecoder>(tmp);

            msg->findInt32("samplerate",&sampleRate);
            msg->findInt32("channel",&channel);
            msg->findInt32("format",&format);

            onInit(sampleRate,channel,format);
            break;
        }
        case kWhatStart:{
            onStart();
            break;
        }
        case kWhatPause:{
            onPause();
            break;
        }
        case kWhatResume:{
            onResume();
            break;
        }
        case kWhatPlay:{
            if(!mIstarted){
                break;
            }
            if(onPlay()){
                std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPlay,shared_from_this());
                msg->post();
            }
            break;
        }
        case kWhatStop:{
            onStop();
            break;
        }
        case kWhatUninit:{
            onUnInit();
            break;
        }
        default:
            break;
    }
}

bool AudioOpenSLESOutput::onInit(int sampleRate, int channel, int format) {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );
    // 创建并初始化 OpenSL ES 引擎
    SLresult result;
    result = slCreateEngine(&mEngineObject, 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to create engine");
        return false;
    }

    result = (*mEngineObject)->Realize(mEngineObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to realize engine");
        return false;
    }

    result = (*mEngineObject)->GetInterface(mEngineObject, SL_IID_ENGINE, &mEngineEngine);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to get engine interface");
        return false;
    }

    // 创建输出混音器
    result = (*mEngineEngine)->CreateOutputMix(mEngineEngine, &mOutputMixObject, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to create output mix");
        return false;
    }

    result = (*mOutputMixObject)->Realize(mOutputMixObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to realize output mix");
        return false;
    }

    // 配置音频源
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {
            SL_DATAFORMAT_PCM,
            2,
            SL_SAMPLINGRATE_44_1,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    // 配置音频接收器
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, mOutputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // 创建音频播放器
    const SLInterfaceID ids[2] = {SL_IID_PLAY, SL_IID_BUFFERQUEUE};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

    result = (*mEngineEngine)->CreateAudioPlayer(mEngineEngine, &mPlayerObject, &audioSrc, &audioSnk, 2, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to create audio player");
        return false;
    }

    result = (*mPlayerObject)->Realize(mPlayerObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to realize audio player");
        return false;
    }

    result = (*mPlayerObject)->GetInterface(mPlayerObject, SL_IID_PLAY, &mPlayerPlay);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to get play interface");
        return false;
    }

    result = (*mPlayerObject)->GetInterface(mPlayerObject, SL_IID_BUFFERQUEUE, &mBufferQueue);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to get buffer queue interface");
        return false;
    }

    // 注册回调函数
    result = (*mBufferQueue)->RegisterCallback(mBufferQueue, bufferQueueCallback, this);
    if (result != SL_RESULT_SUCCESS) {
        LOGI("AudioOpenSLESOutput","Failed to register buffer queue callback");
        return false;
    }

    int len = 512 * 2 * 2;
    uint8_t *buf = (uint8_t *)malloc(len);
    memset(buf,0,len);
    (*mBufferQueue)->Enqueue(mBufferQueue, buf, len);

    mFrameBuf = (uint8_t*) malloc(AUDIO_OUTPUT_FRAMES_SIZE);
    LOGI("AudioOpenSLESOutput","OpenSL ES audio player initialized successfully");
    return false;
}

bool AudioOpenSLESOutput::onStart() {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );
    mIstarted = true;

    if (mPlayerPlay != NULL) {
        SLresult result = (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_PLAYING);
        if (result != SL_RESULT_SUCCESS) {
            LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput Failed to set play state");
            return false;
        }
    }

    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPlay,shared_from_this());
    msg->post();
    return true;
}

bool AudioOpenSLESOutput::onStop() {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );
    mIstarted = false;
    if (mPlayerPlay != NULL) {
        (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_STOPPED);
    }
    return true;
}

bool AudioOpenSLESOutput::onUnInit() {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );
    // 释放资源
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
    if(mFrameBuf){
        free(mFrameBuf);
    }
    return false;
}
bool AudioOpenSLESOutput::onPlay() {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );
    std::unique_lock<std::mutex> lk(mMutex);
    std::shared_ptr<VEFrame> frame = nullptr;
    mAudioDecoder->readFrame(frame);
    if(frame != nullptr){
        if(frame->getFrameType() == E_FRAME_TYPE_EOF){
            ALOGI("AudioOpenSLESOutput::%s - End of Stream (EOS) detected", __FUNCTION__);
            std::shared_ptr<AMessage> eosMsg = mNotify->dup();
            eosMsg->setInt32("type",kWhatEOS);
            eosMsg->post();

            // 停止OpenSLES播放
            if (mPlayerPlay != NULL) {
                (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_STOPPED);
            }
            return false;
        }
        ALOGI("AudioOpenSLESOutput::%s - PTS: %f", __FUNCTION__, frame->getTimestamp());
        SLresult result = (*mBufferQueue)->Enqueue(mBufferQueue, frame->getFrame()->data[0], frame->getFrame()->linesize[0]);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("AudioOpenSLESOutput"," failed: %d", result);
            return false;
        }
        m_AVSync->updateAudioPts(frame->getTimestamp());
    }
    mCond.wait(lk);
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s exit",__FUNCTION__ );
    return true;
}

bool AudioOpenSLESOutput::onPause() {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );
    if (mPlayerPlay != NULL) {
        (*mPlayerPlay)->SetPlayState(mPlayerPlay, SL_PLAYSTATE_PAUSED);
    }
    return false;
}

AudioOpenSLESOutput::AudioOpenSLESOutput(std::shared_ptr<AMessage> notify, std::shared_ptr<VEAVsync> avSync)
:mNotify(notify),m_AVSync(avSync) {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );

}

AudioOpenSLESOutput::~AudioOpenSLESOutput() {
    LOGI("AudioOpenSLESOutput","AudioOpenSLESOutput::%s enter",__FUNCTION__ );

}

void AudioOpenSLESOutput::pause() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatPause,shared_from_this());
    msg->post();
}

void AudioOpenSLESOutput::resume() {
    std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatResume,shared_from_this());
    msg->post();
}

bool AudioOpenSLESOutput::onResume() {

    return false;
}
