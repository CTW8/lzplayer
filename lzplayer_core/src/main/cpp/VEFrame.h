#ifndef __VE_FRAME__
#define __VE_FRAME__
#include<iostream>
#include "VETrace.h"
#include "Log.h"
extern "C"{
    #include"libavcodec/avcodec.h"
    #include "libavutil/pixfmt.h"
    #include "libavutil/channel_layout.h"
}
class VEFrame{
public:
    VEFrame(){
        mFrame = av_frame_alloc();
    }
    ///format :AVPixelFormat and AVSampleFormat
    VEFrame(int width,int height,int format){
        mFrame = av_frame_alloc();
        mFrame->width = width;
        mFrame->height = height;
        mFrame->format = format;

        int ret = av_frame_get_buffer(mFrame,0);
        if (ret < 0) {
            ALOGE("Could not allocate the audio frame data");
            av_frame_free(&mFrame);
        }
    }

    VEFrame(int sampleRate,int channel ,int size,int format){
        mFrame = av_frame_alloc();
        mFrame->sample_rate = sampleRate;
        AVChannelLayout layout;
        av_channel_layout_default(&layout,channel);
        mFrame->ch_layout =layout;
        mFrame->nb_samples = size;
        mFrame->format = format;

        ALOGI("VEFrame sample_rate:%d ch_layout:%d nb_samples:%d format:%d",mFrame->sample_rate,mFrame->ch_layout.nb_channels,mFrame->nb_samples,mFrame->format);

        int ret = av_frame_get_buffer(mFrame,0);
        if (ret < 0) {
            ALOGE("Could not allocate the audio frame data");
            av_frame_free(&mFrame);
        }
    }
    ~VEFrame(){
        if(mFrame){
            ALOGI("AVFrame is release  pts:%" PRId64,mFrame->pts);
//            backTrace();
            av_frame_unref(mFrame);
            av_frame_free(&mFrame);
        }
    }

    AVFrame* getFrame(){
        return mFrame;
    }

    VEFrame& operator=(VEFrame &frame){
        mFrame = frame.getFrame();
        av_frame_ref(mFrame,frame.getFrame());
    }

public:
    uint64_t timestamp=0;

private:
    AVFrame *mFrame;
};
#endif