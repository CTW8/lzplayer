// 组件基类，支持异步操作完成通知

#ifndef __VE_COMPONENT_BASE__
#define __VE_COMPONENT_BASE__

#include "AHandler.h"
#include "AMessage.h"
#include <functional>

namespace VE {

class VEComponentBase : public AHandler {
public:
    enum {
        kWhatStart = 'strt',
        kWhatStop = 'stop',
        kWhatPause = 'paus',
        kWhatResume = 'resm',
        kWhatSeek = 'seek',
        kWhatFlush = 'flsh',
        
        // 完成通知
        kWhatOperationComplete = 'opcm',
        kWhatOperationFailed = 'opfl'
    };
    
    typedef std::function<void(int operation, VEResult result)> CompletionCallback;
    
protected:
    // 通用的操作处理模板
    template<typename Func>
    void handleAsyncOperation(std::shared_ptr<AMessage> msg, 
                             int operation, 
                             Func operationFunc) {
        // 获取回调
        std::shared_ptr<std::function<void()>> callback;
        std::shared_ptr<std::promise<VEResult>> promise;
        std::shared_ptr<AReplyToken> replyToken;
        
        msg->findObject("callback", callback);
        msg->findObject("promise", promise);
        msg->senderAwaitsResponse(replyToken);
        
        // 执行操作
        VEResult result = operationFunc();
        
        // 通知完成
        if (callback && *callback) {
            (*callback)();
        }
        
        if (promise) {
            promise->set_value(result);
        }
        
        if (replyToken) {
            auto response = std::make_shared<AMessage>();
            response->setInt32("result", result);
            response->postReply(replyToken);
        }
        
        // 发送完成通知给 Player
        if (mNotifyMsg) {
            auto notify = mNotifyMsg->dup();
            notify->setInt32("what", kWhatOperationComplete);
            notify->setInt32("operation", operation);
            notify->setInt32("result", result);
            notify->post();
        }
    }
    
    std::shared_ptr<AMessage> mNotifyMsg;
    
public:
    void setNotifyMessage(std::shared_ptr<AMessage> msg) {
        mNotifyMsg = msg;
    }
};

// 示例：改进的 VEDemux
class VEDemuxImproved : public VEComponentBase {
protected:
    void onMessageReceived(const std::shared_ptr<AMessage> &msg) override {
        switch (msg->what()) {
            case kWhatStart:
                handleAsyncOperation(msg, kWhatStart, [this]() {
                    return doStart();
                });
                break;
                
            case kWhatStop:
                handleAsyncOperation(msg, kWhatStop, [this]() {
                    return doStop();
                });
                break;
                
            case kWhatSeek:
                double timestamp;
                msg->findDouble("timestamp", &timestamp);
                handleAsyncOperation(msg, kWhatSeek, [this, timestamp]() {
                    return doSeek(timestamp);
                });
                break;
        }
    }
    
private:
    VEResult doStart() {
        // 实际的启动逻辑
        ALOGI("VEDemux starting...");
        // ... 启动代码 ...
        ALOGI("VEDemux started");
        return VE_OK;
    }
    
    VEResult doStop() {
        // 实际的停止逻辑
        return VE_OK;
    }
    
    VEResult doSeek(double timestamp) {
        // 实际的 seek 逻辑
        return VE_OK;
    }
};

} // namespace VE

#endif // __VE_COMPONENT_BASE__