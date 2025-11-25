//
// Created by 李振 on 2024/6/11.
//

#ifndef LZPLAYER_VEJVMONLOAD_H
#define LZPLAYER_VEJVMONLOAD_H

#include "platform/VEPlatform.h"
#include "Log.h"

#if VE_PLATFORM_ANDROID
#include <jni.h>
#include <sys/prctl.h>

JNIEnv *AttachCurrentThreadEnv();

JNIEnv *AttachCurrentThreadEnvWithName(const char *threadName);
#endif

#endif //LZPLAYER_VEJVMONLOAD_H
