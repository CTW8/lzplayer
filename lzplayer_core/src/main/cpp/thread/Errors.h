/*
 * Copyright (C) 2007 The Android Open Source Project
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

#pragma once

#include <errno.h>
#include <cstdint>
#include <sys/types.h>
#include <string>

/**
 * The type used to return success/failure from frameworks APIs.
 * See the anonymous enum below for valid values.
 */
typedef int32_t status_t;

/*
 * Error codes. 
 * All error codes are negative values.
 */

enum {
    OK                = 0,    // Preferred constant for checking success.
#ifndef NO_ERROR
    // Win32 #defines NO_ERROR as well.  It has the same value, so there's no
    // real conflict, though it's a bit awkward.
    NO_ERROR          = OK,   // Deprecated synonym for `OK`. Prefer `OK` because it doesn't conflict with Windows.
#endif

    UNKNOWN_ERROR       = (-2147483647-1), // INT32_MIN value

    NO_MEMORY           = -ENOMEM,
    INVALID_OPERATION   = -ENOSYS,
    BAD_VALUE           = -EINVAL,
    BAD_TYPE            = (UNKNOWN_ERROR + 1),
    NAME_NOT_FOUND      = -ENOENT,
    PERMISSION_DENIED   = -EPERM,
    NO_INIT             = -ENODEV,
    ALREADY_EXISTS      = -EEXIST,
    DEAD_OBJECT         = -EPIPE,
    FAILED_TRANSACTION  = (UNKNOWN_ERROR + 2),
    BAD_INDEX           = -EOVERFLOW,
    NOT_ENOUGH_DATA     = -ENODATA,
    WOULD_BLOCK         = -EWOULDBLOCK, 
    TIMED_OUT           = -ETIMEDOUT,
    UNKNOWN_TRANSACTION = -EBADMSG,
    FDS_NOT_ALLOWED     = (UNKNOWN_ERROR + 7),
    UNEXPECTED_NULL     = (UNKNOWN_ERROR + 8),
};

// Human readable name of error
std::string statusToString(status_t status);
