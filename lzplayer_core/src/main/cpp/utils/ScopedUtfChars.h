/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __SCOPED_UTF_CHARS__
#define __SCOPED_UTF_CHARS__

#include "nativehelper_utils.h"
#include <string>
#include <cstdio>
#include "Error.h"
namespace VE {
    class ScopedUtfChars {
    public:
        ScopedUtfChars(JNIEnv *env, jstring s) : env_(env), string_(s) {
            if (s == nullptr) {
                utf_chars_ = nullptr;
                jniThrowNullPointerException(env);
            } else {
                utf_chars_ = env->GetStringUTFChars(s, nullptr);
            }
        }

        ScopedUtfChars(ScopedUtfChars &&rhs) noexcept:
                env_(rhs.env_), string_(rhs.string_), utf_chars_(rhs.utf_chars_) {
            rhs.env_ = nullptr;
            rhs.string_ = nullptr;
            rhs.utf_chars_ = nullptr;
        }

        ~ScopedUtfChars() {
            if (utf_chars_) {
                env_->ReleaseStringUTFChars(string_, utf_chars_);
            }
        }

        ScopedUtfChars &operator=(ScopedUtfChars &&rhs) noexcept {
            if (this != &rhs) {
                // Delete the currently owned UTF chars.
                this->~ScopedUtfChars();

                // Move the rhs ScopedUtfChars and zero it out.
                env_ = rhs.env_;
                string_ = rhs.string_;
                utf_chars_ = rhs.utf_chars_;
                rhs.env_ = nullptr;
                rhs.string_ = nullptr;
                rhs.utf_chars_ = nullptr;
            }
            return *this;
        }

        const char *c_str() const {
            return utf_chars_;
        }

        size_t size() const {
            return strlen(utf_chars_);
        }

        const char &operator[](size_t n) const {
            return utf_chars_[n];
        }

    private:
        JNIEnv *env_;
        jstring string_;
        const char *utf_chars_;

        DISALLOW_COPY_AND_ASSIGN(ScopedUtfChars);
    };
}
#endif