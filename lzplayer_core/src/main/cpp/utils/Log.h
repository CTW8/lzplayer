#pragma once

#include "platform/VEPlatform.h"

#if VE_PLATFORM_ANDROID
#include <android/log.h>
#endif

#include <cstdio>

namespace VE {

#define LOG_TAG  "VEPlayer"

#if VE_PLATFORM_ANDROID
    // Android logging using __android_log_print
    #define ALOGW( ...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG,  __VA_ARGS__)
    #define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG,   LOG_TAG, __VA_ARGS__)
    #define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__)
    #define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
    #define ALOGV( ...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else
    // Cross-platform logging using printf
    #define ALOGW(...) do { fprintf(stderr, "[WARN][%s] ", LOG_TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
    #define ALOGD(...) do { fprintf(stdout, "[DEBUG][%s] ", LOG_TAG); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
    #define ALOGI(...) do { fprintf(stdout, "[INFO][%s] ", LOG_TAG); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
    #define ALOGE(...) do { fprintf(stderr, "[ERROR][%s] ", LOG_TAG); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
    #define ALOGV(...) do { fprintf(stdout, "[VERBOSE][%s] ", LOG_TAG); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#endif

} // namespace VE
