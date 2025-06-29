#ifndef __VE_MEDIA_DECODER__
#define __VE_MEDIA_DECODER__
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>

namespace VE {

// 解码器类型枚举
    enum class DecoderType { VIDEO, AUDIO };

// 解码器配置
    struct DecoderConfig {
        DecoderType type;              // VIDEO 或 AUDIO
        std::string codecName;         // 如 "h264", "aac"
        int width = 0, height = 0;     // 视频专用
        int sampleRate = 0, channels = 0; // 音频专用
        std::vector<uint8_t> extraData;   // 如 SPS/PPS 或 AudioSpecificConfig
    };

// 输入帧封装
    struct EncodedFrame {
        std::vector<uint8_t> data;    // 原始 AV 数据
        int64_t pts = 0;              // 显示时间戳
        int64_t dts = 0;              // 解码时间戳
    };

// 输出帧封装
    struct DecodedFrame {
        std::vector<uint8_t> data;    // 原始像素或 PCM 数据
        int width = 0, height = 0;    // 视频专用
        int sampleRate = 0, channels = 0; // 音频专用
        int64_t pts = 0;              // 回调时的时间戳
    };

// 解码结果回调签名
    using FrameCallback = std::function<void(const DecodedFrame&)>;

// 跨平台解码器抽象接口
    class IDecoder {
    public:
        virtual ~IDecoder() = default;

        // 1. 初始化解码器，返回是否成功
        virtual bool initialize(const DecoderConfig& config) = 0;

        // 2. 设置输出回调，解码后自动触发
        virtual void setOutputCallback(FrameCallback callback) = 0;

        // 3. 传入编码帧进行解码，返回是否成功送入解码器
        virtual bool decode(const EncodedFrame& frame) = 0;

        // 4. 刷新内部缓存，用于 seek 或 EOS 前清理
        virtual void flush() = 0;

        // 5. 重置解码器到初始状态，可重新 initialize
        virtual void reset() = 0;

        // 6. 释放所有资源，析构前调用
        virtual void release() = 0;
    };

} // namespace media

#endif