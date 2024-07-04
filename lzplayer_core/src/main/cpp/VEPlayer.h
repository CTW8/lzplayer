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

class VEPlayer : public AHandler
{
public:
    VEPlayer();
    ~VEPlayer();

protected:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

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

    /// setLooping

    /// isLooping

    /// getCurrentPosition

    /// getDuration

    /// setDisplay

    /// setVolume

    /// getTrackInfo

    /// setPlaybackParams

    // int setOnCompletionListener(std::function)

    /// setOnErrorListener

    /// setOnSeekCompleteListener

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
};

#endif