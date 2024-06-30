//
// Created by 李振 on 2024/6/23.
//

#ifndef LZPLAYER_JNISTRING_H
#define LZPLAYER_JNISTRING_H
#include <jni.h>
#include <string>

class JniString {
public:
    JniString(JNIEnv *env, jstring javaString) : env_(env), javaString_(javaString) {
        nativeString_ = javaString ? env->GetStringUTFChars(javaString, nullptr) : nullptr;
    }

    ~JniString() {
        if (nativeString_) {
            env_->ReleaseStringUTFChars(javaString_, nativeString_);
        }
    }

    const char* c_str() const {
        return nativeString_;
    }

private:
    JNIEnv *env_;
    jstring javaString_;
    const char *nativeString_;
};
#endif //LZPLAYER_JNISTRING_H
