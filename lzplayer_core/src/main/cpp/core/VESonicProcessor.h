#ifndef LZPLAYER_VESONICPROCESSOR_H
#define LZPLAYER_VESONICPROCESSOR_H

#include <cstdint>
#include <memory>
#include <vector>
#include "VEError.h"

extern "C" {
#include "sonic.h"
}

namespace VE {

    /// 音频变速处理器（sonic 的薄包装）：改播放速率但不改音调。
    ///
    /// 插在音频渲染器"喂设备"之前，是「IFrameSink 链式插入」思想的最简形态
    /// ——处理级不需要独立线程和 credit，直接在渲染 looper 上同步吞吐。
    ///
    /// 只处理 S16 交织 PCM（设备输出格式固定如此，见 VEAudioOutputConfig）。
    /// 速率为 1.0 时调用方应完全旁路本处理器，零开销。
    class VESonicProcessor {
    public:
        VESonicProcessor() = default;
        ~VESonicProcessor();

        VESonicProcessor(const VESonicProcessor &) = delete;
        VESonicProcessor &operator=(const VESonicProcessor &) = delete;

        /// 建流。重复调用会先销毁旧流(采样率/声道变化时用)。
        VEResult configure(int sampleRate, int channels);

        bool isConfigured() const { return mStream != nullptr; }

        /// 设置速率。0.5~2.0，音调保持不变。
        void setSpeed(float speed);

        float speed() const { return mSpeed; }

        /// 写入一帧 PCM。nbSamples 是"每声道样本数"。
        VEResult write(const uint8_t *pcm, int nbSamples);

        /// 读出已变速的样本到内部 staging 缓冲。
        /// 返回每声道样本数；0 表示还不够攒出输出。
        /// 数据指针经 stagingData() 取，仅在下次 read 前有效。
        int read(int maxSamplesPerChannel);

        const uint8_t *stagingData() const {
            return reinterpret_cast<const uint8_t *>(mStaging.data());
        }

        /// 内部还滞留多少输出样本没被读走(每声道)
        int samplesAvailable() const;

        /// 丢弃内部所有滞留数据。seek / 变速切换 / stop 时必须调用，
        /// 否则旧速率的残留 PCM 会混进新一段播放。
        void flush();

        /// 把滞留样本强制冲出来(EOS 收尾用)，之后 read 能取到尾巴
        void drain();

    private:
        void destroyStream();

        sonicStream mStream = nullptr;
        int mSampleRate = 0;
        int mChannels = 0;
        float mSpeed = 1.0f;
        /// read 的输出暂存区，按需增长
        std::vector<short> mStaging;
    };
}

#endif //LZPLAYER_VESONICPROCESSOR_H
