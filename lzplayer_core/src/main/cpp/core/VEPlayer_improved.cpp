// 示例代码：改进的 VEPlayer 实现

#include "VEPlayer.h"
#include <atomic>

namespace VE {

// 方案一：使用原子计数器追踪组件完成状态
class VEPlayerImproved : public VEPlayer {
private:
    // 组件完成状态追踪
    struct ComponentSync {
        std::atomic<int> pendingCount{0};
        std::atomic<int> completedCount{0};
        std::function<void()> onAllCompleted;
        std::mutex mutex;
        
        void reset(int totalComponents) {
            pendingCount = totalComponents;
            completedCount = 0;
        }
        
        void notifyCompleted() {
            completedCount++;
            if (completedCount == pendingCount) {
                if (onAllCompleted) {
                    onAllCompleted();
                }
            }
        }
    };
    
    ComponentSync mStartSync;
    ComponentSync mStopSync;
    ComponentSync mPauseSync;
    ComponentSync mSeekSync;

public:
    // 改进的 start 实现
    VEResult onStartImproved(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        
        // 计算需要启动的组件数量
        int componentCount = 0;
        if (mVideoRender) componentCount++;
        if (mAudioOutput) componentCount++;
        if (mVideoDecoder) componentCount++;
        if (mAudioDecoder) componentCount++;
        if (mDemux) componentCount++;
        
        // 重置同步计数器
        mStartSync.reset(componentCount);
        
        // 设置所有组件完成后的回调
        mStartSync.onAllCompleted = [this]() {
            ALOGI("All components started successfully");
            // 这里可以通知上层 start 完成
            notifyInfo(VE_PLAYER_INFO_STARTED, 0, 0, "", nullptr);
        };
        
        // 并行启动所有组件，每个组件完成后回调
        if (mVideoRender) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mVideoRender);
            msg->setObject("callback", std::make_shared<std::function<void()>>([this]() {
                mStartSync.notifyCompleted();
            }));
            msg->post();
        }
        
        if (mAudioOutput) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mAudioOutput);
            msg->setObject("callback", std::make_shared<std::function<void()>>([this]() {
                mStartSync.notifyCompleted();
            }));
            msg->post();
        }
        
        if (mVideoDecoder) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mVideoDecoder);
            msg->setObject("callback", std::make_shared<std::function<void()>>([this]() {
                mStartSync.notifyCompleted();
            }));
            msg->post();
        }
        
        if (mAudioDecoder) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mAudioDecoder);
            msg->setObject("callback", std::make_shared<std::function<void()>>([this]() {
                mStartSync.notifyCompleted();
            }));
            msg->post();
        }
        
        if (mDemux) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mDemux);
            msg->setObject("callback", std::make_shared<std::function<void()>>([this]() {
                mStartSync.notifyCompleted();
            }));
            msg->post();
        }
        
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }
};

// 方案二：使用 Promise/Future 机制
class VEPlayerWithFuture : public VEPlayer {
private:
    std::vector<std::future<VEResult>> mPendingOperations;
    
public:
    VEResult onStartWithFuture(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        
        mPendingOperations.clear();
        
        // 创建异步任务
        if (mVideoRender) {
            auto promise = std::make_shared<std::promise<VEResult>>();
            mPendingOperations.push_back(promise->get_future());
            
            auto msg = std::make_shared<AMessage>(kWhatStart, mVideoRender);
            msg->setObject("promise", promise);
            msg->post();
        }
        
        if (mAudioOutput) {
            auto promise = std::make_shared<std::promise<VEResult>>();
            mPendingOperations.push_back(promise->get_future());
            
            auto msg = std::make_shared<AMessage>(kWhatStart, mAudioOutput);
            msg->setObject("promise", promise);
            msg->post();
        }
        
        // 继续为其他组件创建任务...
        
        // 在后台线程等待所有操作完成
        std::thread([this]() {
            bool allSuccess = true;
            for (auto& future : mPendingOperations) {
                VEResult result = future.get();
                if (result != VE_OK) {
                    allSuccess = false;
                }
            }
            
            // 通知完成
            if (allSuccess) {
                notifyInfo(VE_PLAYER_INFO_STARTED, 0, 0, "", nullptr);
            } else {
                onErrorCallback(VE_PLAYER_ERROR_START_FAILED, "Some components failed to start");
            }
        }).detach();
        
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }
};

// 方案三：使用状态机 + 消息反馈
class VEPlayerWithStateMachine : public VEPlayer {
private:
    enum OperationState {
        IDLE,
        STARTING,
        STARTED,
        STOPPING,
        STOPPED,
        PAUSING,
        PAUSED,
        SEEKING,
        SEEK_COMPLETE
    };
    
    struct OperationTracker {
        OperationState state = IDLE;
        std::set<std::string> pendingComponents;
        std::set<std::string> completedComponents;
        std::function<void()> onComplete;
        std::function<void(const std::string&)> onError;
        
        void reset(OperationState newState) {
            state = newState;
            pendingComponents.clear();
            completedComponents.clear();
        }
        
        void addComponent(const std::string& name) {
            pendingComponents.insert(name);
        }
        
        void markCompleted(const std::string& name) {
            pendingComponents.erase(name);
            completedComponents.insert(name);
            
            if (pendingComponents.empty() && onComplete) {
                onComplete();
            }
        }
        
        bool isComplete() const {
            return pendingComponents.empty();
        }
    };
    
    OperationTracker mOperationTracker;
    
public:
    VEResult onStartWithTracking(std::shared_ptr<AMessage> msg) {
        ALOGI("VEPlayer::%s enter", __FUNCTION__);
        
        // 初始化操作追踪器
        mOperationTracker.reset(STARTING);
        
        // 注册需要启动的组件
        if (mVideoRender) mOperationTracker.addComponent("VideoRender");
        if (mAudioOutput) mOperationTracker.addComponent("AudioOutput");
        if (mVideoDecoder) mOperationTracker.addComponent("VideoDecoder");
        if (mAudioDecoder) mOperationTracker.addComponent("AudioDecoder");
        if (mDemux) mOperationTracker.addComponent("Demux");
        
        // 设置完成回调
        mOperationTracker.onComplete = [this]() {
            mOperationTracker.state = STARTED;
            ALOGI("All components started");
            notifyInfo(VE_PLAYER_INFO_STARTED, 0, 0, "", nullptr);
        };
        
        // 并行发送启动消息
        if (mVideoRender) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mVideoRender);
            msg->setString("component", "VideoRender");
            msg->post();
        }
        
        if (mAudioOutput) {
            auto msg = std::make_shared<AMessage>(kWhatStart, mAudioOutput);
            msg->setString("component", "AudioOutput");
            msg->post();
        }
        
        // 继续为其他组件发送消息...
        
        ALOGI("VEPlayer::%s exit", __FUNCTION__);
        return VE_OK;
    }
    
    // 处理组件完成通知
    void onComponentNotify(std::shared_ptr<AMessage> msg) {
        std::string component;
        int32_t what;
        
        if (msg->findString("component", component) && msg->findInt32("what", &what)) {
            switch (what) {
                case kWhatStartComplete:
                    mOperationTracker.markCompleted(component);
                    break;
                case kWhatStopComplete:
                    // 处理停止完成
                    break;
                // 其他操作...
            }
        }
    }
};

} // namespace VE