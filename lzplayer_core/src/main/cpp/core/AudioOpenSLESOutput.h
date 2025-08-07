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
#include "core/VEAudioDecoder.h"
#include "core/VERingBuffer.h"
#include "core/VEAVsync.h"


void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);


namespace VE {
    class AudioOpenSLESOutput : public IVEComponent{
        friend void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

    public:
        AudioOpenSLESOutput(std::shared_ptr<AMessage> notify, std::shared_ptr<VEAVsync> avSync);

        ~AudioOpenSLESOutput();

        VEResult prepare(std::shared_ptr<VEAudioDecoder> decoder, int samplerate, int channel, int format);

        VEResult prepare(VEBundle params) override;

        VEResult start() override;

        VEResult pause() override;

        VEResult stop() override;

        VEResult release() override;

        VEResult seekTo(double timestamp) override;

        VEResult flush() override;

    public:
        friend void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

        enum {
            kWhatEOS = 'aeos',
            kWhatError = 'aerr'
        };

    private:
        bool onInit(int sampleRate, int channel, int format);

        bool onStart();

        bool onPlay();

        bool onPause();

        bool onStop();

        bool onUnInit();

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        enum {
            kWhatPrepare = 'prep',
            kWhatStart = 'star',
            kWhatPause = 'paus',
            kWhatSeek  =  'seek',
            kWhatStop = 'stop',
            kWhatPlay = 'play',
            kWhatRelease = 'rele'
        };
    private:
        bool mIstarted = false;
        std::shared_ptr<VEAudioDecoder> mAudioDecoder = nullptr;
        SLObjectItf mEngineObject = NULL;
        SLEngineItf mEngineEngine = NULL;
        SLObjectItf mOutputMixObject = NULL;
        SLObjectItf mPlayerObject = NULL;
        SLPlayItf mPlayerPlay = NULL;
        SLAndroidSimpleBufferQueueItf mBufferQueue = NULL;

        std::shared_ptr<VEAVsync> m_AVSync;
        std::shared_ptr<AMessage> mNotify = nullptr;

        std::mutex mMutex;
        std::condition_variable mCond;

        VERingBuffer *mRingBuffer = nullptr;
        uint8_t *mFrameBuf = nullptr;
        FILE *fp = nullptr;
    };
}

#endif //LZPLAYER_AUDIOOPENSLESOUTPUT_H
