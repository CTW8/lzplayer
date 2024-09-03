//
// Created by 李振 on 2024/7/7.
//

#ifndef LZPLAYER_VEMEDIACLOCK_H
#define LZPLAYER_VEMEDIACLOCK_H
#include "Log.h"
#include <iostream>

class VEMediaClock {
public:
    VEMediaClock();
    ~VEMediaClock();

    void updateAudioPts(double timestamp);
    double getVideoRenderTime(double vpts);

private:
    double m_AudioTimestamp=0;
    double m_VideoTimeStamp=0;
    int64_t m_VideoSystemTime=0;
    int64_t m_AudioSystemTime=0;

    std::mutex m_Mutex;
};


#endif //LZPLAYER_VEMEDIACLOCK_H
