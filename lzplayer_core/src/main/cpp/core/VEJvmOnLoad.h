//
// Created by 李振 on 2024/6/11.
//

#ifndef LZPLAYER_VEJVMONLOAD_H
#define LZPLAYER_VEJVMONLOAD_H
#include <jni.h>
#include <sys/prctl.h>
#include "Log.h"

JNIEnv *AttachCurrentThreadEnv();

JNIEnv *AttachCurrentThreadEnvWithName(const char *threadName);

#endif //LZPLAYER_VEJVMONLOAD_H
