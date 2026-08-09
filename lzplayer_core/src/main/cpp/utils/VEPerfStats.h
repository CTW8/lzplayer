#ifndef LZPLAYER_VEPERFSTATS_H
#define LZPLAYER_VEPERFSTATS_H

#include <atomic>
#include <memory>
#include <string>

#include <chrono>

#include "VEPerfHistogram.h"

namespace VE {

    /// 打点统一用 steady_clock。禁用 wall clock 的理由同 VEStartupTrace：
    /// NTP 校准会让差值出现负数
    inline int64_t nowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }


    /// 稳态性能指标的共享容器：由 VEPlayer 建好后注入各组件，
    /// 组件只管往自己那一项里 add()，VEPlayer 统一序列化。
    ///
    /// 为什么用一个共享对象而不是给每个接口加一堆 getter：视频解码器有软解
    /// 与硬解两个实现、显示端也有两条路径(硬解的显示在解码器内部)，若靠
    /// getter 取，VEPlayer 就得对每种组合做 dynamic_cast 分支。共享容器让
    /// "谁写"和"谁读"彻底解耦——换一个解码器实现不需要改读取侧一行代码。
    ///
    /// 每个直方图只由一个 looper 写入(单写者)，因此内部无锁。
    struct VEPerfStats {
        /// 单帧解码的 **CPU 成本**，只有软解会填(avcodec_receive_frame +
        /// 必要的像素格式转换)。硬解不填——见下面的 codecLatencyUs。
        VEPerfHistogram videoDecodeUs{0, 100, 512};      // 0~51.2ms，桶宽 0.1ms
        VEPerfHistogram audioDecodeUs{0, 50, 256};       // 0~12.8ms，桶宽 0.05ms

        /// 硬解 codec 的端到端延迟：包入队到对应输出出队。
        ///
        /// **这不是"解码耗时"，别拿它和 videoDecodeUs 比。** 它的绝大部分是
        /// 背压等待——解码器故意跑在同步时钟之前，输出队列一满就停止取输出，
        /// 于是输入包在 codec 里排队。实测能到 380ms 量级，而真正的解码工作
        /// 只占其中很小一部分。
        ///
        /// 它的用处是判断 codec 是否跟得上：这个值持续增长说明产能不足；
        /// 稳定在一个和缓冲深度成正比的水平上则属正常。
        VEPerfHistogram codecLatencyUs{0, 2000, 512};    // 0~1024ms，桶宽 2ms
        /// 上屏耗时：软解为 renderFrame 往返，硬解为 releaseOutputBuffer 往返
        VEPerfHistogram presentUs{0, 100, 256};          // 0~25.6ms
        /// present 的三段拆分。**合成一个数字无法指导优化**：上传是 CPU 拷贝
        /// (零拷贝能省)，swap 是等 vsync 的阻塞(零拷贝一分钱省不下来)。
        /// 不拆开就可能为了 5.7ms 去写一大堆 AHardwareBuffer 代码，
        /// 而实际大头在 swap 里。
        VEPerfHistogram uploadUs{0, 100, 256};           // 三平面纹理上传
        VEPerfHistogram drawUs{0, 100, 256};             // 顶点/uniform/绘制
        VEPerfHistogram swapUs{0, 200, 256};             // eglSwapBuffers(0~51.2ms)
        /// 同步余量：帧应显示时刻减实际提交时刻。**正=提前就绪(健康)，
        /// 负=已迟到**。它是丢帧的前兆——丢帧还是 0 但 p95 逼近 0 时，
        /// 再多一点负载就会开始掉帧，这是丢帧计数给不出的预警。
        VEPerfHistogram syncMarginUs{-50000, 500, 400};  // -50~150ms，桶宽 0.5ms

        /// 队列峰值。必须在 seek/换源时随直方图一起清零，否则一次 seek
        /// 抖动出的峰值会挂在整段播放的读数上
        std::atomic<int> audioQueuePeak{0};
        std::atomic<int> videoQueuePeak{0};
        std::atomic<int> frameQueuePeak{0};

        /// 丢帧按**原因**分类。原先只有一个 droppedFrames，而它只统计了
        /// "同步判定太晚"这一种——队列溢出兜底、代次过期、seek 追帧三条丢帧
        /// 路径完全没入账。于是面板上"丢帧 0"的真实含义是"没有因迟到而丢帧"，
        /// 不是"没丢帧"，又是一个名字与实际度量不符的读数。
        ///
        /// 分开记是因为对策完全不同：
        ///   late        解码或渲染跟不上 → 查解码耗时/上屏耗时
        ///   overflow    credit 记账失守 → 查回执链路，属异常
        ///   stale       flush/seek 后的在途旧帧 → 正常，只是别当成问题
        ///   seekCatchup 精准 seek 追帧丢的 → seek 性能的一部分，不是缺陷
        std::atomic<int64_t> dropLate{0};
        std::atomic<int64_t> dropOverflow{0};
        std::atomic<int64_t> dropStale{0};
        std::atomic<int64_t> dropSeekCatchup{0};

        void reset() {
            videoDecodeUs.reset();
            audioDecodeUs.reset();
            codecLatencyUs.reset();
            presentUs.reset();
            uploadUs.reset();
            drawUs.reset();
            swapUs.reset();
            syncMarginUs.reset();
            audioQueuePeak.store(0, std::memory_order_relaxed);
            videoQueuePeak.store(0, std::memory_order_relaxed);
            frameQueuePeak.store(0, std::memory_order_relaxed);
            dropLate.store(0, std::memory_order_relaxed);
            dropOverflow.store(0, std::memory_order_relaxed);
            dropStale.store(0, std::memory_order_relaxed);
            dropSeekCatchup.store(0, std::memory_order_relaxed);
        }

        /// 只涨不落地更新峰值
        static void bumpPeak(std::atomic<int> &peak, int value) {
            int prev = peak.load(std::memory_order_relaxed);
            while (value > prev &&
                   !peak.compare_exchange_weak(prev, value, std::memory_order_relaxed)) {
                // prev 已被 compare_exchange_weak 更新为当前值，循环重试
            }
        }

        /// 追加进 stats JSON(不含外层花括号)，末尾不带逗号
        std::string toJsonFragment() const {
            std::string out;
            out.reserve(512);
            videoDecodeUs.appendJson(out, "videoDecodeMs");
            out += ",";
            codecLatencyUs.appendJson(out, "codecLatencyMs");
            out += ",";
            audioDecodeUs.appendJson(out, "audioDecodeMs");
            out += ",";
            presentUs.appendJson(out, "presentMs");
            out += ",";
            uploadUs.appendJson(out, "uploadMs");
            out += ",";
            drawUs.appendJson(out, "drawMs");
            out += ",";
            swapUs.appendJson(out, "swapMs");
            out += ",";
            syncMarginUs.appendJson(out, "syncMarginMs");
            char buf[128];
            snprintf(buf, sizeof(buf),
                     ",\"audioQueuePeak\":%d,\"videoQueuePeak\":%d,\"frameQueuePeak\":%d"
                     ",\"dropLate\":%lld,\"dropOverflow\":%lld"
                     ",\"dropStale\":%lld,\"dropSeekCatchup\":%lld",
                     audioQueuePeak.load(std::memory_order_relaxed),
                     videoQueuePeak.load(std::memory_order_relaxed),
                     frameQueuePeak.load(std::memory_order_relaxed),
                     (long long) dropLate.load(std::memory_order_relaxed),
                     (long long) dropOverflow.load(std::memory_order_relaxed),
                     (long long) dropStale.load(std::memory_order_relaxed),
                     (long long) dropSeekCatchup.load(std::memory_order_relaxed));
            out += buf;
            return out;
        }
    };
}

#endif //LZPLAYER_VEPERFSTATS_H
