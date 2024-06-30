#pragma once
#include <iostream>
#include <cstdarg>
#include <cstdio>

#include <android/log.h>
#define LOG_TAG  "VEPlayer"
// 定义日志宏
#define ALOGW(format, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, format, ##__VA_ARGS__)
#define ALOGD(format, ...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, format, ##__VA_ARGS__)
#define ALOGI(format, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, format, ##__VA_ARGS__)
#define ALOGE(format, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, format, ##__VA_ARGS__)
#define ALOGV(format, ...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, format, ##__VA_ARGS__)
