//
// Created by 李振 on 2025/7/2.
//

#ifndef LZPLAYER_VEAUDIOTRACK_H
#define LZPLAYER_VEAUDIOTRACK_H

#include "IAudioRender.h"
namespace VE {
    class VEAudioTrack : public IAudioRender {
    public:
        VEAudioTrack(){}
        ~VEAudioTrack() override = default;

        VEResult configure(const AudioConfig &config) override;

        VEResult start() override;

        VEResult pause() override;

        VEResult flush() override;

        VEResult stop() override;

        VEResult renderFrame(std::shared_ptr<VEFrame> frame) override;

        VEResult release() override;
    };

}
#endif //LZPLAYER_VEAUDIOTRACK_H
