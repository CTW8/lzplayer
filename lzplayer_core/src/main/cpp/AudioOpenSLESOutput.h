//
// Created by 李振 on 2024/6/30.
//

#ifndef LZPLAYER_AUDIOOPENSLESOUTPUT_H
#define LZPLAYER_AUDIOOPENSLESOUTPUT_H

#include "thread/Errors.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "VEAudioDecoder.h"


void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

class AudioOpenSLESOutput: public AHandler{
public:
    AudioOpenSLESOutput();
    ~AudioOpenSLESOutput();

    status_t init(std::shared_ptr<VEAudioDecoder> decoder,int samplerate,int channel,int format);
    void start();
    void pause();
    void stop();
    void unInit();

    friend void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

private:
    bool onInit(int sampleRate,int channel,int format);
    bool onStart();
    bool onPlay();
    bool onPause();
    bool onStop();
    bool onUnInit();
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

    enum {
        kWhatInit                = 'init',
        kWhatStart               = 'star',
        kWhatPause               = 'paus',
        kWhatStop                = 'stop',
        kWhatPlay                = 'play',
        kWhatUninit              = 'unin'
    };
private:
    bool       mIstarted = false;
    std::shared_ptr<VEAudioDecoder> mAudioDecoder = nullptr;
    SLObjectItf mEngineObject = NULL;
    SLEngineItf mEngineEngine = NULL;
    SLObjectItf mOutputMixObject = NULL;
    SLObjectItf mPlayerObject = NULL;
    SLPlayItf mPlayerPlay = NULL;
    SLAndroidSimpleBufferQueueItf mBufferQueue = NULL;


    std::mutex  mMutex;
    std::condition_variable mCond;
    FILE *fp = nullptr;
};


#endif //LZPLAYER_AUDIOOPENSLESOUTPUT_H
