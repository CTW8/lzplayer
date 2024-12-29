#ifndef      __VE_DEMUX__
#define      __VE_DEMUX__

#include<string>
#include<memory>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include "VEPacketQueue.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEError.h"
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}

class VEDemux : public AHandler
{

public:
    VEDemux();
    ~VEDemux();

    VEResult open(std::string file);
    void start();
    void stop();
    void pause();
    void resume();
    void needMorePacket(std::shared_ptr<AMessage> msg,int type);
    VEResult read(bool isAudio,std::shared_ptr<VEPacket> &packet);
    VEResult seek(double posMs);
    VEResult close();
    std::shared_ptr<VEMediaInfo> getFileInfo();

private:
    VEResult onOpen(std::string path);
    VEResult onStart();
    VEResult onRead();
    VEResult onSeek(double posMs);
    void putPacket(std::shared_ptr<VEPacket> packet,bool isAudio);
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

private:
    std::string mFilePath;
    int32_t mWidth=0;
    int32_t mHeight=0;
    uint64_t mDuration=0;
    int32_t mFps=0;
    AVCodecParameters *mVideoCodecParams=nullptr;
    AVRational mVideoTimeBase;
    int64_t mVStartTime=0;

    int32_t mSampleRate=0;
    int32_t mChannel=0;
    int32_t mSampleFormat=0;
    AVCodecParameters *mAudioCodecParams=nullptr;
    AVRational mAudioTimeBase;
    int64_t mAStartTime=0;

    int mAudio_index=-1;
    int mVideo_index=-1;

    bool mIsStart = false;
    bool mIsPause = false;

    bool mNeedAudioMore = false;
    bool mNeedVideoMore = false;

    std::shared_ptr<AMessage> mAudioNotify = nullptr;
    std::shared_ptr<AMessage> mVideoNotify = nullptr;

    std::mutex mMutexAudio;
    std::condition_variable mCondAudio;

    std::mutex mMutexVideo;
    std::condition_variable mCondVideo;

    int64_t mAudioStartPts=0;
    int64_t mVideoStartPts =0;

    //视频帧
    std::shared_ptr<VEPacketQueue> mVideoPacketQueue = nullptr;
    //音频帧
    std::shared_ptr<VEPacketQueue> mAudioPacketQueue = nullptr;
    AVFormatContext* mFormatContext=nullptr;


private:
    enum {
        kWhatOpen                = 'open',
        kWhatStart               = 'star',
        kWhatStop                = 'stop',
        kWhatEOS                 = 'eos ',
        kWhatPause               = 'paus',
        kWhatResume              = 'resm',
        kWhatSeek                = 'seek',
        kWhatRead                = 'read'
    };

};

#endif