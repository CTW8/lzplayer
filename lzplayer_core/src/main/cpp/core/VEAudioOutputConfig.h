#ifndef LZPLAYER_VEAUDIOOUTPUTCONFIG_H
#define LZPLAYER_VEAUDIOOUTPUTCONFIG_H

extern "C" {
#include "libavutil/samplefmt.h"
}

namespace VE {

    /// 音频输出格式。解码器按它重采样，渲染器按它配置设备，
    /// 两边必须用同一份配置，所以在这里统一算出来。
    struct VEAudioOutputConfig {
        int sampleRate;
        int channels;
        int format;   ///< AVSampleFormat
    };

    /// OpenSL ES 只能接受这些采样率
    inline bool isSupportedOutputSampleRate(int sampleRate) {
        switch (sampleRate) {
            case 8000: case 11025: case 16000: case 22050:
            case 24000: case 32000: case 44100: case 48000:
            case 64000: case 88200: case 96000: case 192000:
                return true;
            default:
                return false;
        }
    }

    /// 根据源音频参数决定输出参数。
    /// 源采样率本身就被设备支持时直接透传——48kHz 素材相当常见，
    /// 无脑重采样到 44.1kHz 既费 CPU 又损音质。
    inline VEAudioOutputConfig chooseAudioOutputConfig(int srcSampleRate, int srcChannels) {
        VEAudioOutputConfig config;
        config.sampleRate = isSupportedOutputSampleRate(srcSampleRate) ? srcSampleRate : 44100;
        // 只做单声道/立体声，多声道下混为立体声
        config.channels = (srcChannels == 1) ? 1 : 2;
        // OpenSL ES 的 PCM 接口固定 16bit
        config.format = AV_SAMPLE_FMT_S16;
        return config;
    }
}

#endif //LZPLAYER_VEAUDIOOUTPUTCONFIG_H
