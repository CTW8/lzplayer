#pragma once
#include <iostream>
#include <cstdarg>
#include <cstdio>
// 定义日志宏
#define ALOGW(format, ...) logMessage("WARN", format, ##__VA_ARGS__)
#define ALOGD(format, ...) logMessage("DEBUG", format, ##__VA_ARGS__)
#define ALOGI(format, ...) logMessage("INFO", format, ##__VA_ARGS__)
#define ALOGE(format, ...) logMessage("ERROR", format, ##__VA_ARGS__)
#define ALOGV(format, ...) logMessage("VERBOSE", format, ##__VA_ARGS__)

// 日志函数实现
inline void logMessage(const char* level, const char* format, ...) {
    va_list args;
    va_start(args, format);

    // 打印日志级别
    std::cout << "[" << level << "] ";

    // 打印格式化消息
    vprintf(format, args);
    std::cout << std::endl;

    va_end(args);
}
