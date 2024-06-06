#ifndef __VEPLAYER__
#define __VEPLAYER__

#include <string>
#include "VEDemux.h"
#include "VEAudioDecoder.h"
#include "VEVideoDecoder.h"
#include "VEPacket.h"
#include "VEFrame.h"
#include "VEPacketQueue.h"
#include "VEFrameQueue.h"
#include "VEResult.h"
class VEPlayer
{
public:
    VEPlayer();
    ~VEPlayer();

    /// setDataSource
    int setDataSource(std::string path);

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
    VEDemux *mDemux = nullptr;
    VEAudioDecoder *mAudioDecoder = nullptr;
    VEVideoDecoder *mVideoDecoder = nullptr;
    VEPacketQueue *mAPacketQueue=nullptr;
    VEPacketQueue *mVPacketQueue=nullptr;

    VEFrameQueue *mAFrameQueue=nullptr;
    VEFrameQueue *mVFrameQueue=nullptr;

    shared_ptr<VEMediaInfo> mMediaInfo=nullptr;

    string mPath;
};

#endif