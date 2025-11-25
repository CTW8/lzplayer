//
// VEPlatform.h - Cross-platform definitions and abstractions
// This file provides platform detection and common type definitions
//

#ifndef __VE_PLATFORM_H__
#define __VE_PLATFORM_H__

// Platform detection
#if defined(__ANDROID__)
    #define VE_PLATFORM_ANDROID 1
    #define VE_PLATFORM_NAME "Android"
#elif defined(__linux__)
    #define VE_PLATFORM_LINUX 1
    #define VE_PLATFORM_NAME "Linux"
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define VE_PLATFORM_IOS 1
        #define VE_PLATFORM_NAME "iOS"
    #else
        #define VE_PLATFORM_MACOS 1
        #define VE_PLATFORM_NAME "macOS"
    #endif
#elif defined(_WIN32) || defined(_WIN64)
    #define VE_PLATFORM_WINDOWS 1
    #define VE_PLATFORM_NAME "Windows"
#else
    #define VE_PLATFORM_UNKNOWN 1
    #define VE_PLATFORM_NAME "Unknown"
#endif

// Default values for undefined platforms
#ifndef VE_PLATFORM_ANDROID
    #define VE_PLATFORM_ANDROID 0
#endif
#ifndef VE_PLATFORM_LINUX
    #define VE_PLATFORM_LINUX 0
#endif
#ifndef VE_PLATFORM_IOS
    #define VE_PLATFORM_IOS 0
#endif
#ifndef VE_PLATFORM_MACOS
    #define VE_PLATFORM_MACOS 0
#endif
#ifndef VE_PLATFORM_WINDOWS
    #define VE_PLATFORM_WINDOWS 0
#endif

// OpenGL ES availability
#if VE_PLATFORM_ANDROID || VE_PLATFORM_IOS
    #define VE_HAS_GLES 1
#else
    #define VE_HAS_GLES 0
#endif

// OpenGL availability (Desktop)
#if VE_PLATFORM_LINUX || VE_PLATFORM_MACOS || VE_PLATFORM_WINDOWS
    #define VE_HAS_GL 1
#else
    #define VE_HAS_GL 0
#endif

namespace VE {

// Forward declaration of platform-specific window handle
// Each platform will define its own type
struct NativeWindowHandle;

// Platform-independent window wrapper
class VEWindow {
public:
    virtual ~VEWindow() = default;
    
    // Get native handle - platform specific implementation
    virtual void* getNativeHandle() const = 0;
    
    // Get window dimensions
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    
    // Set window dimensions
    virtual void setSize(int width, int height) = 0;
};

} // namespace VE

#endif // __VE_PLATFORM_H__
