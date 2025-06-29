//
// Created by 李振 on 2024/8/31.
//

#include "TimeUtils.h"
namespace VE {
    uint64_t TimeUtils::getCurrentTimeMs() {
        auto now = std::chrono::system_clock::now();
        // 将时间点转换为微秒
        auto duration_in_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch());

        return static_cast<uint64_t>(duration_in_milliseconds.count());
    }
}
