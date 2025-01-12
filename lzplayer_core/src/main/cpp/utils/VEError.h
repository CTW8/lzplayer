//
// Created by 李振 on 2024/12/28.
//

#ifndef LZPLAYER_VEERROR_H
#define LZPLAYER_VEERROR_H
#include <stdint.h>
enum {
    VE_OK                = 0,    // Preferred constant for checking success.

    VE_NO_ERROR          = VE_OK,   // Deprecated synonym for `OK`. Prefer `OK` because it doesn't conflict with Windows.


    VE_UNKNOWN_ERROR       = (-2147483647-1), // INT32_MIN value

    VE_NO_MEMORY           = (VE_UNKNOWN_ERROR + 1),
    VE_INVALID_OPERATION   = (VE_UNKNOWN_ERROR + 2),
    VE_BAD_VALUE           = (VE_UNKNOWN_ERROR + 3),
    VE_BAD_TYPE            = (VE_UNKNOWN_ERROR + 4),
    VE_NAME_NOT_FOUND      = (VE_UNKNOWN_ERROR + 5),
    VE_PERMISSION_DENIED   = (VE_UNKNOWN_ERROR + 6),
    VE_NO_INIT             = (VE_UNKNOWN_ERROR + 7),
    VE_ALREADY_EXISTS      = (VE_UNKNOWN_ERROR + 8),
    VE_DEAD_OBJECT         = (VE_UNKNOWN_ERROR + 9),
    VE_FAILED_TRANSACTION  = (VE_UNKNOWN_ERROR + 10),
    VE_BAD_INDEX           = (VE_UNKNOWN_ERROR + 11),
    VE_NOT_ENOUGH_DATA     = (VE_UNKNOWN_ERROR + 12),
    VE_WOULD_BLOCK         = (VE_UNKNOWN_ERROR + 13),
    VE_TIMED_OUT           = (VE_UNKNOWN_ERROR + 14),
    VE_UNKNOWN_TRANSACTION = (VE_UNKNOWN_ERROR + 15),
    VE_FDS_NOT_ALLOWED     = (VE_UNKNOWN_ERROR + 16),
    VE_UNEXPECTED_NULL     = (VE_UNKNOWN_ERROR + 17),
    VE_INVALID_PARAMS      = (VE_UNKNOWN_ERROR + 18),
    VE_EOS                 = (VE_UNKNOWN_ERROR + 19),
    VE_ERROR_EAGAIN        = (VE_UNKNOWN_ERROR + 20),
};

typedef int32_t VEResult;
#endif //LZPLAYER_VEERROR_H
