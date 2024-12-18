#ifndef __I_VIDEO_RENDERER__
#define __I_VIDEO_RENDERER__

#include <memory>
#include "VEFrame.h"
#include "../thread/AMessage.h"
#include "../VEFrame.h"

class IVideoRenderer {
public:
    virtual ~IVideoRenderer() = default;

    // 初始化渲染器，platformData 可用于传递平台特定的数据（如窗口句柄、Surface等）
    virtual int initialize(void* platformData) = 0;

    // 配置渲染参数，如分辨率和帧率
    virtual int configure(int width, int height, int fps) = 0;

    // 启动渲染过程
    virtual void start() = 0;

    // 停止渲染过程
    virtual void stop() = 0;

    // 提交一帧进行渲染
    virtual void renderFrame(const std::shared_ptr<VEFrame>& frame) = 0;

    // 设置渲染通知消息
    virtual void setNotify(const std::shared_ptr<AMessage>& notify) = 0;

    // 释放渲染器资源
    virtual int uninitialize() = 0;
};

#endif 