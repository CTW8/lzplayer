#ifndef __VE_AUDIO_RENDER__
#define __VE_AUDIO_RENDER__

#include "IAudioRender.h"
#include "VEAudioDecoder.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>

class VEAudioRender : public AHandler {
public:
    VEAudioRender(std::shared_ptr<IAudioRender> audioRenderer, std::shared_ptr<VEAudioDecoder> audioDecoder);
    ~VEAudioRender();

    // 初始化音频渲染
    int init(const AudioConfig &config);

    // 开始播放
    int start();

    // 停止播放
    int stop();

    // 渲染音频帧
    void renderFrame(std::shared_ptr<VEFrame> frame);

protected:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

private:
    std::shared_ptr<IAudioRender> m_AudioRenderer; // 音频渲染器接口
    std::shared_ptr<VEAudioDecoder> m_AudioDecoder;  // 音频解码器
    std::deque<std::shared_ptr<VEFrame>> m_FrameQueue; // PCM帧队列
    std::mutex m_Mutex;
    std::condition_variable m_Cond;

    // 声明 fetchAudioData 函数
    void fetchAudioData();

    // 消息类型
    enum {
        kWhatInit = 'init',
        kWhatStart = 'star',
        kWhatStop = 'stop',
        kWhatRender = 'rend',
        kWhatFetchData = 'fdat',
    };
};

#endif