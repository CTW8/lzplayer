#ifndef __I_VIDEO_RENDERER__
#define __I_VIDEO_RENDERER__

#include <memory>
#include "platform/VEPlatform.h"
#include "core/VEFrame.h"
#include "../thread/AMessage.h"
#include "core/VEFrame.h"
#include "VEBundle.h"
#include "VEError.h"

#if VE_PLATFORM_ANDROID
#include <android/native_window_jni.h>
#endif

namespace VE {

    // Platform-independent native window type
#if VE_PLATFORM_ANDROID
    typedef ANativeWindow* VENativeWindow;
#elif VE_PLATFORM_LINUX
    typedef void* VENativeWindow;  // X11 Window or Wayland surface
#elif VE_PLATFORM_WINDOWS
    typedef void* VENativeWindow;  // HWND
#elif VE_PLATFORM_MACOS || VE_PLATFORM_IOS
    typedef void* VENativeWindow;  // CALayer or UIView
#else
    typedef void* VENativeWindow;
#endif

    class IVideoRender {
    public:
        virtual ~IVideoRender() = default;

        // Initialize renderer with platform-specific data (window handle, Surface, etc.)
        virtual VEResult initialize(VEBundle params) = 0;

        // Change the rendering surface/window
        virtual VEResult changeSurface(VENativeWindow win, int viewWidth, int viewHeight) = 0;

        // Submit a frame for rendering
        virtual VEResult renderFrame(const std::shared_ptr<VEFrame> &frame) = 0;

        // Release renderer resources
        virtual VEResult uninitialize() = 0;
    };
}

#endif 