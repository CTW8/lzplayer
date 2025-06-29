#pragma once
#include <android/log.h>

namespace VE {

#define LOG_TAG  "VEPlayer"

// 标准 C++11 可变参宏（不再吞逗号）
// 注意：调用时必须至少传入“格式串”这一参数，否则会编译失败
#define ALOGW( ...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG,  __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG,   LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGV( ...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
} // namespace VE