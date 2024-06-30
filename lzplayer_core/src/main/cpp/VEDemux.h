#ifndef      __VE_DEMUX__
#define      __VE_DEMUX__

#include<string>
#include<memory>
#include<deque>
#include"VEMediaDef.h"
#include"VEPacket.h"
#include "AHandler.h"
#include "AMessage.h"
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

    status_t open(std::string file);
    void start();
    void stop();
    void pause();
    void resume();
    status_t read(bool isAudio,std::shared_ptr<VEPacket> &packet);
    status_t seek(uint64_t pos);
    status_t close();
    std::shared_ptr<VEMediaInfo> getFileInfo();

private:
    status_t onOpen(std::string path);
    status_t onStart();
    status_t onRead();
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

    std::mutex mMutexAudio;
    std::condition_variable mCondAudio;

    std::mutex mMutexVideo;
    std::condition_variable mCondVideo;

    //视频帧
    std::deque<std::shared_ptr<VEPacket>> mVideoPacketQueue;
    //音频帧
    std::deque<std::shared_ptr<VEPacket>> mAudioPacketQueue;
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