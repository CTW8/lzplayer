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
#include "VEResult.h"
#include "jni.h"
#include "thread/AHandler.h"
#include "VEVideoRender.h"
#include "AudioOpenSLESOutput.h"

typedef std::function<void(int type,int msg1,double msg2,std::string msg3,void *msg4)> funOnInfoCallback;
typedef std::function<void(int type,int code,std::string msg)> funOnErrorCallback;
typedef std::function<void(int progress)> funOnProgressCallback;

class VEPlayer
{
public:
    VEPlayer();
    ~VEPlayer();

//protected:
//    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

public:
    /// setDataSource
    int setDataSource(std::string path);

    int setDisplayOut(ANativeWindow* win,int viewWidth,int viewHeight);

    /// prepare
    int prepare();

    /// start
    int start();

    /// stop
    int stop();

    /// pause
    int pause();

    /// resume
    int resume();

    /// release
    int release();

    /// seekTo
    int seek(int64_t timestamp);

    /// reset
    int reset();

    void setLooping();

    long getCurrentPosition();

    long getDuration();

    void setVolume(int volume);

    /// setPlaybackParams

    void setOnInfoListener(funOnInfoCallback callback);

    void setOnErrorListener(funOnErrorCallback callback);

    void setOnProgressListener(funOnProgressCallback callback);

    void notifyInfo(int type,int msg1,double msg2,std::string msg3,void *msg4){
        if(onInfoCallback){
            onInfoCallback(type,msg1,msg2,msg3,msg4);
        }
    }

    void notifyError(int type,int code,std::string msg){
        if(onErrorCallback){
            onErrorCallback(type,code,msg);
        }
    }

    void notifyProgress(int64_t progress){
        if(onProgressCallback){
            onProgressCallback(progress);
        }
    }

private:
    pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;
    std::shared_ptr<VEDemux> mDemux = nullptr;
    std::shared_ptr<ALooper> mDemuxLooper = nullptr;
    std::shared_ptr<VEAudioDecoder> mAudioDecoder = nullptr;
    std::shared_ptr<ALooper> mAudioDecodeLooper = nullptr;
    std::shared_ptr<VEVideoDecoder> mVideoDecoder = nullptr;
    std::shared_ptr<ALooper> mVideoDecodeLooper = nullptr;
    std::shared_ptr<VEVideoRender> mVideoRender = nullptr;
    std::shared_ptr<ALooper>  mVideoRenderLooper = nullptr;

    std::shared_ptr<AudioOpenSLESOutput> mAudioOutput = nullptr;
    std::shared_ptr<ALooper> mAudioOutputLooper = nullptr;

    std::shared_ptr<VEPacketQueue> mAPacketQueue=nullptr;

    std::shared_ptr<VEMediaInfo> mMediaInfo=nullptr;

    std::string mPath;

    ANativeWindow *mWindow = nullptr;
    int mViewWidth = 0;
    int mViewHeight = 0;

    funOnProgressCallback onProgressCallback;
    funOnErrorCallback  onErrorCallback;
    funOnInfoCallback onInfoCallback;
};

#endif