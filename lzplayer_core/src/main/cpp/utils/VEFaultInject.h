#ifndef LZPLAYER_VEFAULTINJECT_H
#define LZPLAYER_VEFAULTINJECT_H

#include <atomic>

#include "Log.h"

namespace VE {

    /// 故障注入（decoder-test-redesign.md §2）。
    ///
    /// 存在的理由：**运行期硬解 fallback 自 Phase 2 起从未触发过一次**，
    /// 是 high-perf-player 硬指标里唯一的空白。它不是"还没排上"，而是
    /// 没有触发手段——只能等一台恰好不支持某编码的设备，或一个恰好损坏的
    /// 文件。那不叫测试。
    ///
    /// 两条纪律：
    ///
    /// **编译期隔离**。Release 构建里连代码都不存在。播放器的故障注入一旦
    /// 能在生产包里被打开，就是一个可被利用的稳定性开关。用
    /// `-PveFaultInject=true` 打开。
    ///
    /// **状态可见**。注入生效时必须留痕，否则一次忘记关闭的注入会让后续
    /// 所有测试结论作废，而且极难发现——本项目已经在"注入什么都没测到却
    /// 报 PASS"上栽过三次（stall 场景），那还只是没测到；注入忘关是反过来
    /// 的，测到的全是假的。
    class VEFaultInject {
    public:
        /// 硬解创建失败（AMediaCodec_createDecoderByType 返回前置空）
        static std::atomic<bool> sFailHwCreate;
        /// 硬解配置失败（AMediaCodec_configure 返回前改错误码）
        static std::atomic<bool> sFailHwConfigure;
        /// 运行期失败：第 N 帧后 dequeueOutputBuffer 返回错误。
        /// **这条是最难触发、也最该测的** —— 建链期失败还有工厂兜底，
        /// 运行期失败要求播放器在播放中途无缝重建为软解且不中断。
        /// 0 = 不注入
        static std::atomic<int> sFailHwAfterFrames;

        /// 有任何一项生效即为真。报告的环境指纹必须记录它
        static bool active() {
            return sFailHwCreate.load(std::memory_order_relaxed) ||
                   sFailHwConfigure.load(std::memory_order_relaxed) ||
                   sFailHwAfterFrames.load(std::memory_order_relaxed) > 0;
        }

        /// 每次 prepare 后复位，防止忘关的注入污染后续用例
        static void reset() {
            if (active()) {
                ALOGW("VEFaultInject: reset (had active injection)");
            }
            sFailHwCreate.store(false, std::memory_order_relaxed);
            sFailHwConfigure.store(false, std::memory_order_relaxed);
            sFailHwAfterFrames.store(0, std::memory_order_relaxed);
        }

        static void dump(const char *where) {
            if (active()) {
                ALOGW("VEFAULT active at %s: create=%d configure=%d afterFrames=%d",
                      where,
                      (int) sFailHwCreate.load(std::memory_order_relaxed),
                      (int) sFailHwConfigure.load(std::memory_order_relaxed),
                      sFailHwAfterFrames.load(std::memory_order_relaxed));
            }
        }
    };

#if defined(VE_ENABLE_FAULT_INJECTION)
    /// 注入判定宏。Release 下整段编译期消失，连分支都不产生
    #define VE_FAULT_HW_CREATE()    (VEFaultInject::sFailHwCreate.load(std::memory_order_relaxed))
    #define VE_FAULT_HW_CONFIGURE() (VEFaultInject::sFailHwConfigure.load(std::memory_order_relaxed))
    #define VE_FAULT_HW_AFTER(n)    (VEFaultInject::sFailHwAfterFrames.load(std::memory_order_relaxed) > 0 && \
                                     (n) >= VEFaultInject::sFailHwAfterFrames.load(std::memory_order_relaxed))
#else
    #define VE_FAULT_HW_CREATE()    (false)
    #define VE_FAULT_HW_CONFIGURE() (false)
    #define VE_FAULT_HW_AFTER(n)    (false)
#endif

}

#endif //LZPLAYER_VEFAULTINJECT_H
