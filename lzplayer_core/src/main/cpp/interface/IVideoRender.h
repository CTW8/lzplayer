#ifndef __I_VIDEO_RENDERER__
#define __I_VIDEO_RENDERER__

#include <memory>
#include <android/native_window_jni.h>
#include "core/VEFrame.h"
#include "../thread/AMessage.h"
#include "core/VEFrame.h"
#include "VEBundle.h"
#include "VEError.h"

namespace VE {
    class IVideoRender {
    public:
        virtual ~IVideoRender() = default;

        // 初始化渲染器，platformData 可用于传递平台特定的数据（如窗口句柄、Surface等）
        virtual VEResult initialize(VEBundle params) = 0;

        virtual VEResult changeSurface(ANativeWindow *win,int viewWidth,int viewHeight) = 0;

        // 提交一帧进行渲染
        virtual VEResult renderFrame(const std::shared_ptr<VEFrame> &frame) = 0;

        // 释放渲染器资源
        virtual VEResult uninitialize() = 0;
    };
}

#endif 