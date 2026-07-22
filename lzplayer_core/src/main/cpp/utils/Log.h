#pragma once
#include <android/log.h>

namespace VE {

#define LOG_TAG  "VEPlayer"

// 解码/渲染路径上每帧都会打若干条日志，__android_log_print 本身的开销
// (格式化 + 写 socket)在 30~60fps 下相当可观。Release 构建里把 V/D/I
// 三级编译期剔除，只保留 W/E；Debug 构建保持全量输出。
//
// 想在 Release 包上临时打开详细日志，编译时加 -DVE_FORCE_VERBOSE_LOG 即可。
#if defined(NDEBUG) && !defined(VE_FORCE_VERBOSE_LOG)
#define VE_LOG_VERBOSE_ENABLED 0
#else
#define VE_LOG_VERBOSE_ENABLED 1
#endif

// 警告和错误任何构建下都保留：它们只在异常路径上出现，且是线上排查的唯一线索
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#if VE_LOG_VERBOSE_ENABLED
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG,   LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else
// 用 do{}while(0) 吞掉参数：既不产生代码，又能保持参数的语法检查，
// 避免只在 Debug 下编译通过的日志语句。
#define ALOGD(...) do { if (0) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGI(...) do { if (0) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGV(...) do { if (0) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__); } while (0)
#endif

} // namespace VE
