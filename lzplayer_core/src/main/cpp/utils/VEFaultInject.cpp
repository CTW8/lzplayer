#include "VEFaultInject.h"

namespace VE {
    std::atomic<bool> VEFaultInject::sFailHwCreate{false};
    std::atomic<bool> VEFaultInject::sFailHwConfigure{false};
    std::atomic<int> VEFaultInject::sFailHwAfterFrames{0};
}
