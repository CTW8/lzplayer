#ifndef LZPLAYER_VEAAUDIORENDER_H
#define LZPLAYER_VEAAUDIORENDER_H

// AAudio 的类型定义可以直接用(纯头文件)，但函数一律经 dlsym 取——
// 直接链接会让 libaaudio 变成 DT_NEEDED 硬依赖，API 24/25 机型加载不了本 .so
#include <aaudio/AAudio.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "IAudioRender.h"

namespace VE {

    /// AAudio 音频后端（API 26+）。
    ///
    /// 相比 OpenSL ES 的两个实质改进：
    /// 1) 低延迟模式直通快速混音路径，功耗与延迟都更好；
    /// 2) `AAudioStream_getTimestamp` 给出真实已呈现帧数，音频时钟不必
    ///    再靠"块数 × 估算时长 + 拍脑袋的器件延迟"——这是音画同步精度的
    ///    根本改善(SLES 拿不到这个信息)。
    ///
    /// AAudio 是拉模型(设备回调来要数据)，而上层是推模型，所以这里用一个
    /// 内部环形缓冲对接：renderFrame 往里写，data callback 从里取。
    class VEAAudioRender : public IAudioRender {
    public:
        VEAAudioRender();
        ~VEAAudioRender() override;

        VEResult configure(const AudioConfig &config) override;
        VEResult start() override;
        VEResult pause() override;
        VEResult flush() override;
        VEResult stop() override;
        VEResult renderFrame(std::shared_ptr<VEFrame> frame) override;
        int64_t getQueuedDurationUs() override;
        VEResult release() override;

        /// 运行期是否可用：dlopen libaaudio 成功且符号齐全。
        /// API 24/25 机型上返回 false，工厂据此退回 SLES。
        static bool isAvailable();

    private:
        static aaudio_data_callback_result_t dataCallback(
                AAudioStream *stream, void *userData, void *audioData, int32_t numFrames);
        static void errorCallback(AAudioStream *stream, void *userData, aaudio_result_t error);

        /// 环形缓冲里可读的字节数(调用方须持锁)
        size_t readableLocked() const;
        void closeStream();

        AAudioStream *mStream = nullptr;
        AudioConfig mConfig{};
        bool mConfigured = false;
        std::atomic<bool> mPlaying{false};

        /// PCM 环形缓冲：上层写、设备回调读
        std::vector<uint8_t> mRing;
        size_t mReadPos = 0;
        size_t mWritePos = 0;
        bool mFull = false;
        std::mutex mMutex;

        int mBytesPerFrame = 4;   ///< 声道数 × 每样本字节数
    };
}

#endif //LZPLAYER_VEAAUDIORENDER_H
