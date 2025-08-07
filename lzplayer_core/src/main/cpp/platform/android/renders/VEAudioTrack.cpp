//
// Created by 李振 on 2025/7/2.
//

#include "VEAudioTrack.h"
namespace VE{

    VEResult VEAudioTrack::configure(const AudioConfig &config) {
        return 0;
    }

    VEResult VEAudioTrack::start() {
        return 0;
    }

    VEResult VEAudioTrack::pause() {
        return 0;
    }

    VEResult VEAudioTrack::flush() {
        return 0;
    }

    VEResult VEAudioTrack::stop() {
        return 0;
    }

    VEResult VEAudioTrack::renderFrame(std::shared_ptr<VEFrame> frame) {
        return 0;
    }

    VEResult VEAudioTrack::release() {
        return 0;
    }
}
