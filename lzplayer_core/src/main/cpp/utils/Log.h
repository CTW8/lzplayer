#pragma once
#include <android/log.h>

namespace VE {

#define LOG_TAG  "VEPlayer"

// 解码/渲染路径上每帧都会打若干条日志，__android_log_print 本身的开销
// (格式化 + 写 socket)在 30~60fps 下相当可观。Release 构建里把 V/D/I
// 三级编译期剔除，只保留 W/E；Debug 构建保持全量输出。
//
// 想在 Release 包上临时打开详细日志，编译时加 -DVE_FORCE_VERBOSE_LOG 即可。
//
// 反过来，Debug 包上想量"日志本身吃了多少 CPU"，加 -DVE_QUIET_LOG 即可
// 剔除 V/D/I 而保留 Debug 的其它特性——这是做日志开销对照实验的开关，
// 不改构建类型就能对比，避免把 NDEBUG 带来的其它差异混进来。
#if defined(VE_QUIET_LOG) || (defined(NDEBUG) && !defined(VE_FORCE_VERBOSE_LOG))
#define VE_LOG_VERBOSE_ENABLED 0
#else
#define VE_LOG_VERBOSE_ENABLED 1
#endif

// 每帧/每包日志单列一档，**默认关闭**，即使 Debug 构建也不打。
//
// 为什么不能跟 ALOGV 混在一起：设备 logcat 有每进程行数配额(ColorOS 是
// 300 行/秒)，一条每帧日志在 30fps 下就是 30 行/秒，七八条这样的点位足以
// 独占整个配额——真正有用的事件日志(状态迁移、回退、EOS、错误)会被静默
// 丢弃。于是"日志越多越看不见东西"：出问题时打开 logcat 满屏都是 pts。
//
// 而 VE_QUIET_LOG 是另一个极端，它把 V/D/I 一起编掉，事件日志也没了。
// 两档之间缺的正是"少而有意义"，ALOGF 就是为了把这一档分出来：
//   默认         → 事件日志齐全，无每帧噪声（日常开发与测试用这个）
//   -DVE_TRACE_FRAME → 额外打开每帧日志（只在追单帧问题时用）
//   -DVE_QUIET_LOG   → 连事件日志一起编掉（量日志自身开销的对照实验）
#if defined(VE_TRACE_FRAME) && VE_LOG_VERBOSE_ENABLED
#define VE_LOG_FRAME_ENABLED 1
#else
#define VE_LOG_FRAME_ENABLED 0
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

// 每帧/每包日志专用。参数同样保留语法检查，避免只在 VE_TRACE_FRAME 下能编过。
#if VE_LOG_FRAME_ENABLED
#define ALOGF(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else
#define ALOGF(...) do { if (0) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__); } while (0)
#endif

} // namespace VE
