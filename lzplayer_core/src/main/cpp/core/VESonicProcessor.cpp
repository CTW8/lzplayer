#include "VESonicProcessor.h"
#include "utils/Log.h"

namespace VE {

    VESonicProcessor::~VESonicProcessor() {
        destroyStream();
    }

    void VESonicProcessor::destroyStream() {
        if (mStream) {
            sonicDestroyStream(mStream);
            mStream = nullptr;
        }
    }

    VEResult VESonicProcessor::configure(int sampleRate, int channels) {
        if (sampleRate <= 0 || channels <= 0) {
            return VE_INVALID_PARAMS;
        }
        if (mStream && mSampleRate == sampleRate && mChannels == channels) {
            return VE_OK;   // 参数没变，沿用现有流
        }
        destroyStream();
        mStream = sonicCreateStream(sampleRate, channels);
        if (mStream == nullptr) {
            ALOGE("VESonicProcessor::%s sonicCreateStream failed (%dHz %dch)",
                  __FUNCTION__, sampleRate, channels);
            return VE_NO_MEMORY;
        }
        mSampleRate = sampleRate;
        mChannels = channels;
        sonicSetSpeed(mStream, mSpeed);
        // 音调保持原样——这正是"变速不变调"的关键，只调 speed 不调 pitch
        sonicSetPitch(mStream, 1.0f);
        ALOGI("VESonicProcessor::%s configured %dHz %dch speed=%.2f",
              __FUNCTION__, sampleRate, channels, mSpeed);
        return VE_OK;
    }

    void VESonicProcessor::setSpeed(float speed) {
        mSpeed = speed;
        if (mStream) {
            sonicSetSpeed(mStream, speed);
        }
    }

    VEResult VESonicProcessor::write(const uint8_t *pcm, int nbSamples) {
        if (mStream == nullptr || pcm == nullptr || nbSamples <= 0) {
            return VE_INVALID_PARAMS;
        }
        // sonic 的 numSamples 是每声道样本数，与 AVFrame::nb_samples 一致
        if (sonicWriteShortToStream(mStream,
                                    reinterpret_cast<const short *>(pcm),
                                    nbSamples) == 0) {
            ALOGE("VESonicProcessor::%s sonicWriteShortToStream failed", __FUNCTION__);
            return VE_NO_MEMORY;
        }
        return VE_OK;
    }

    int VESonicProcessor::read(int maxSamplesPerChannel) {
        if (mStream == nullptr || maxSamplesPerChannel <= 0) {
            return 0;
        }
        const size_t need = static_cast<size_t>(maxSamplesPerChannel) * mChannels;
        if (mStaging.size() < need) {
            mStaging.resize(need);
        }
        return sonicReadShortFromStream(mStream, mStaging.data(), maxSamplesPerChannel);
    }

    int VESonicProcessor::samplesAvailable() const {
        return mStream ? sonicSamplesAvailable(mStream) : 0;
    }

    void VESonicProcessor::flush() {
        if (mStream == nullptr) {
            return;
        }
        // sonic 没有"丢弃"接口：先 flush 把滞留数据推到输出侧，
        // 再整段读空，等价于清零内部状态
        sonicFlushStream(mStream);
        int available = sonicSamplesAvailable(mStream);
        while (available > 0) {
            const int got = read(available);
            if (got <= 0) {
                break;
            }
            available = sonicSamplesAvailable(mStream);
        }
    }

    void VESonicProcessor::drain() {
        if (mStream) {
            sonicFlushStream(mStream);
        }
    }
}
