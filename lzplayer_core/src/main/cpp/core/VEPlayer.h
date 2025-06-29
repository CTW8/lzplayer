#ifndef __VEPLAYER__
#define __VEPLAYER__

#include <string>
#include <android/native_window_jni.h>
#include "VEDemux.h"
#include "VEAudioDecoder.h"
#include "VEVideoDecoder.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEPacketQueue.h"
#include "VEFrameQueue.h"
#include "VEError.h"
#include "jni.h"
#include "thread/AHandler.h"
#include "VEVideoRender.h"
#include "AudioOpenSLESOutput.h"
#include "VEDef.h"


typedef std::function<void(int code,double arg1,std::string str1,void *obj3)> funOnInfoCallback;
typedef std::function<void(int code,std::string msg)> funOnErrorCallback;
typedef std::function<void(void)> funOnCompletionCallback;
typedef std::function<void(double progress)> funOnProgressCallback;
typedef std::function<void()> funOnEOSCallback;
typedef std::function<void()> funOnPreparedCallback;
typedef std::function<void()> funOnSeekComplateCallback;

namespace VE {
    class VEPlayer : public AHandler {
    public:
        VEPlayer();

        ~VEPlayer();

    private:
        void onRenderNotify(std::shared_ptr<AMessage> msg);

        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        void onEOS();

    public:
        /// setDataSource
        VEResult setDataSource(std::string path);

        VEResult setDisplayOut(ANativeWindow *win, int viewWidth, int viewHeight);

        /// prepare
        VEResult prepare();

        VEResult prepareAsync();

        /// start
        VEResult start();

        /// stop
        VEResult stop();

        /// pause
        VEResult pause();

        /// release
        VEResult release();

        /// seekTo
        VEResult seek(double timestampMs);

        /// reset
        VEResult reset();

        void setLooping(bool enable);

        long getCurrentPosition();

        long getDuration();

        void setVolume(int volume);

        VEResult setPlaySpeed(float speed);

        /// setPlaybackParams

        void setOnInfoListener(funOnInfoCallback callback);

        void setOnProgressListener(funOnProgressCallback callback);

        void setOnErrorListener(funOnErrorCallback callback);

        void setOnCompletionListener(funOnCompletionCallback callback);

        void setOnEOSListener(funOnEOSCallback callback);

        void setOnPreparedListener(funOnPreparedCallback callback);

        void setOnSeekComplateListener(funOnSeekComplateCallback callback);

        void notifyInfo(int type, int msg1, double msg2, std::string msg3, void *msg4) {
            if (onInfoCallback) {
                onInfoCallback(msg1, msg2, msg3, msg4);
            }
        }

        void notifyProgress(int64_t progress) {
            if (onProgressCallback) {
                onProgressCallback((double) progress * 1000.f / AV_TIME_BASE);
            }
        }

    private:
        enum {
            kWhatSetDataSource = '=DaS',
            kWhatPrepare = 'prep',
            kWhatSetVideoSurface = '=VSu',
            kWhatStart = 'strt',
            kWhatVideoNotify = 'vidN',
            kWhatAudioNotify = 'audN',
            kWhatClosedCaptionNotify = 'capN',
            kWhatRendererNotify = 'renN',
            kWhatReset = 'rset',
            kWhatSeek = 'seek',
            kWhatPause = 'paus',
            kWhatStop = 'stop',
            kWhatRenderEvent = 'renE',
            kWhatVideoDecEvent = 'vdec',
            kWhatAudioDecEvent = 'adec',
            kWhatDemuxEvent = 'demx',
            kWhatRelease = 'rele'
        };

        VEResult onSetDataSource(std::shared_ptr<AMessage> msg);

        VEResult onPrepare(std::shared_ptr<AMessage> msg);

        VEResult onStart(std::shared_ptr<AMessage> msg);

        VEResult onStop(std::shared_ptr<AMessage> msg);

        VEResult onPause(std::shared_ptr<AMessage> msg);

        VEResult onSeek(std::shared_ptr<AMessage> msg);

        VEResult onReset(std::shared_ptr<AMessage> msg);

        VEResult onRelease(std::shared_ptr<AMessage> msg);

        pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;
        std::shared_ptr<VEDemux> mDemux = nullptr;
        std::shared_ptr<ALooper> mDemuxLooper = nullptr;
        std::shared_ptr<VEAudioDecoder> mAudioDecoder = nullptr;
        std::shared_ptr<ALooper> mAudioDecodeLooper = nullptr;
        std::shared_ptr<VEVideoDecoder> mVideoDecoder = nullptr;
        std::shared_ptr<ALooper> mVideoDecodeLooper = nullptr;
        std::shared_ptr<VEVideoRender> mVideoRender = nullptr;
        std::shared_ptr<ALooper> mVideoRenderLooper = nullptr;
        std::shared_ptr<VEAVsync> mAVSync = nullptr;

        std::shared_ptr<AMessage> mRenderNotifyMsg = nullptr;

        std::shared_ptr<AudioOpenSLESOutput> mAudioOutput = nullptr;
        std::shared_ptr<ALooper> mAudioOutputLooper = nullptr;

        std::shared_ptr<VEPacketQueue> mAPacketQueue = nullptr;

        std::shared_ptr<VEMediaInfo> mMediaInfo = nullptr;

        std::string mPath;

        bool mVideoEOS = false;
        bool mAudioEOS = false;

        bool mEnableLoop = false;

        ANativeWindow *mWindow = nullptr;
        int mViewWidth = 0;
        int mViewHeight = 0;

        funOnProgressCallback onProgressCallback;
        funOnInfoCallback onInfoCallback;
        funOnErrorCallback onErrorCallback;
        funOnCompletionCallback onCompleteCallback;
        funOnEOSCallback onEosCallback;
        funOnPreparedCallback onPreparedCallback;
        funOnSeekComplateCallback onSeekComplateCallback;
    };
}

#endif