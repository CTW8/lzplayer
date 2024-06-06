#ifndef      __VE_DEMUX__
#define      __VE_DEMUX__

#include<string>
#include<memory>
#include"VEMediaDef.h"
#include"VEPacket.h"
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}
using namespace std;
class VEDemux
{

public:
    VEDemux();
    ~VEDemux();

    int32_t open(std::string file);
    int32_t read(VEPacket *packet);
    int32_t seek(uint64_t pos);
    int32_t close();
    shared_ptr<VEMediaInfo> getFileInfo();
private:
/* data */
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

    int mAudio_index=0;
    int mVideo_index=0;

    AVFormatContext* mFormatContext=nullptr;
};

#endif