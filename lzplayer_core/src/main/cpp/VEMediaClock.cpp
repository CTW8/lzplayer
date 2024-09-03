//
// Created by 李振 on 2024/7/7.
//

#include "VEMediaClock.h"
#include "TimeUtils.h"
#include "ALooper.h"

VEMediaClock::VEMediaClock() {

}

VEMediaClock::~VEMediaClock() {

}

void VEMediaClock::updateAudioPts(double timestamp) {
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_AudioSystemTime = ALooper::GetNowUs();
}

double VEMediaClock::getVideoRenderTime(double vpts) {
    std::lock_guard<std::mutex> lk(m_Mutex);

    return 0;
}
