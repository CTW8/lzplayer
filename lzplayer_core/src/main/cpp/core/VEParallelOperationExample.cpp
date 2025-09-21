// 并行操作的完整示例

#include "VEPlayer.h"
#include <future>
#include <vector>

namespace VE {

// 方案四：使用协程风格的实现（C++20之前的替代方案）
class VEPlayerParallel : public VEPlayer {
private:
    // 操作令牌，用于追踪和取消操作
    class OperationToken {
    public:
        std::atomic<bool> cancelled{false};
        std::atomic<int> pendingCount{0};
        std::promise<VEResult> promise;
        std::future<VEResult> future;
        
        OperationToken() : future(promise.get_future()) {}
        
        void cancel() {
            cancelled = true;
        }
        
        bool isCancelled() const {
            return cancelled.load();
        }
    };
    
    std::shared_ptr<OperationToken> mCurrentOperation;
    
public:
    // 最优方案：结合多种机制
    VEResult onStartParallel(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        
        // 取消之前的操作（如果有）
        if (mCurrentOperation) {
            mCurrentOperation->cancel();
        }
        
        // 创建新的操作令牌
        mCurrentOperation = std::make_shared<OperationToken>();
        auto token = mCurrentOperation;
        
        // 计算组件数量
        std::vector<std::pair<std::string, std::shared_ptr<AHandler>>> components;
        if (mVideoRender) components.push_back({"VideoRender", mVideoRender});
        if (mAudioOutput) components.push_back({"AudioOutput", mAudioOutput});
        if (mVideoDecoder) components.push_back({"VideoDecoder", mVideoDecoder});
        if (mAudioDecoder) components.push_back({"AudioDecoder", mAudioDecoder});
        if (mDemux) components.push_back({"Demux", mDemux});
        
        token->pendingCount = components.size();
        
        // 并行启动所有组件
        for (const auto& [name, component] : components) {
            auto startMsg = std::make_shared<AMessage>(kWhatStart, component);
            
            // 设置完成回调
            startMsg->setObject("callback", std::make_shared<std::function<void(VEResult)>>(
                [token, name, this](VEResult result) {
                    if (token->isCancelled()) {
                        return;
                    }
                    
                    if (result != VE_OK) {
                        ALOGE("Component %s failed to start: %d", name.c_str(), result);
                        token->promise.set_value(result);
                        return;
                    }
                    
                    int remaining = --token->pendingCount;
                    ALOGI("Component %s started, %d remaining", name.c_str(), remaining);
                    
                    if (remaining == 0) {
                        // 所有组件启动完成
                        token->promise.set_value(VE_OK);
                        
                        // 在 Player 的消息队列中处理完成事件
                        auto completeMsg = std::make_shared<AMessage>(kWhatStartComplete, shared_from_this());
                        completeMsg->post();
                    }
                }
            ));
            
            startMsg->post();
        }
        
        // 可选：设置超时
        std::thread([token, this]() {
            auto status = token->future.wait_for(std::chrono::seconds(5));
            if (status == std::future_status::timeout) {
                ALOGE("Start operation timeout");
                onErrorCallback(VE_PLAYER_ERROR_TIMEOUT, "Start operation timeout");
            }
        }).detach();
        
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }
    
    // 处理 seek 的特殊情况：需要有序执行
    VEResult onSeekParallel(std::shared_ptr<AMessage> msg) {
        double timestamp;
        msg->findDouble("timestamp", &timestamp);
        
        ALOGI("VEPlayer::%s seek to %f", __FUNCTION__, timestamp);
        
        // Seek 需要特殊处理：
        // 1. 先暂停数据流
        // 2. 清理缓冲区
        // 3. seek 到新位置
        // 4. 恢复数据流
        
        auto token = std::make_shared<OperationToken>();
        mCurrentOperation = token;
        
        // 第一阶段：并行暂停所有组件
        std::atomic<int> pauseCount{5};
        auto pauseComplete = std::make_shared<std::promise<void>>();
        auto pauseFuture = pauseComplete->get_future();
        
        auto pauseCallback = [&pauseCount, pauseComplete]() {
            if (--pauseCount == 0) {
                pauseComplete->set_value();
            }
        };
        
        // 发送暂停消息
        sendPauseToAllComponents(pauseCallback);
        
        // 等待暂停完成后执行 seek
        std::thread([this, token, timestamp, pauseFuture = std::move(pauseFuture)]() mutable {
            // 等待暂停完成
            pauseFuture.wait();
            
            if (token->isCancelled()) return;
            
            // 第二阶段：清理和 seek
            // Demux 先 seek
            auto demuxSeekComplete = std::make_shared<std::promise<VEResult>>();
            auto demuxMsg = std::make_shared<AMessage>(kWhatSeek, mDemux);
            demuxMsg->setDouble("timestamp", timestamp);
            demuxMsg->setObject("promise", demuxSeekComplete);
            demuxMsg->post();
            
            VEResult demuxResult = demuxSeekComplete->get_future().get();
            if (demuxResult != VE_OK) {
                onErrorCallback(VE_PLAYER_ERROR_SEEK_FAILED, "Demux seek failed");
                return;
            }
            
            // 第三阶段：并行 flush 解码器和渲染器
            flushAllComponents();
            
            // 第四阶段：恢复播放
            resumeAllComponents();
            
            // 通知 seek 完成
            onSeekComplateCallback();
            
        }).detach();
        
        return VE_OK;
    }
    
private:
    void sendPauseToAllComponents(std::function<void()> callback) {
        // 实现暂停所有组件
    }
    
    void flushAllComponents() {
        // 实现清理所有组件缓冲区
    }
    
    void resumeAllComponents() {
        // 实现恢复所有组件
    }
    
    // 新增的消息处理
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override {
        switch (msg->what()) {
            case kWhatStartComplete:
                ALOGI("All components started successfully");
                notifyInfo(VE_PLAYER_INFO_STARTED, 0, 0, "", nullptr);
                break;
                
            default:
                VEPlayer::onMessageReceived(msg);
                break;
        }
    }
    
    enum {
        kWhatStartComplete = 'stcm',
    };
};

} // namespace VE