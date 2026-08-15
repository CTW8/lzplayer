#ifndef LZPLAYER_VEPERFSTATS_H
#define LZPLAYER_VEPERFSTATS_H

#include <atomic>
#include <memory>
#include <string>

#include <chrono>
#include <cinttypes>

#include "VEPerfHistogram.h"
#include "Log.h"

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
        /// credit 用尽(park)次数。**判断上下游谁是瓶颈的关键判据**：
        /// 频繁 park 说明渲染端消费慢(解码有余力)，从不 park 说明解码是瓶颈。
        /// 这个判断此前只能靠猜。
        std::atomic<int64_t> videoCreditPark{0};
        std::atomic<int64_t> audioCreditPark{0};

        /// 上游饥饿次数与每次持续时长。次数多但时长短=供给抖动；
        /// 时长长=源确实供不上(网络源的核心指标)
        std::atomic<int64_t> videoStarve{0};
        std::atomic<int64_t> audioStarve{0};
        VEPerfHistogram starveUs{0, 1000, 512};          // 0~512ms，桶宽 1ms

        /// 连续两帧实际上屏时刻的间隔。**比帧率更接近主观体验**——帧率均值
        /// 正常而间隔抖动大完全可能发生，而抖动才是用户看到的卡顿感。
        /// 30fps 应稳定在 33.3ms，p95 与 p50 的差就是抖动幅度。
        VEPerfHistogram presentIntervalUs{0, 500, 512};  // 0~256ms，桶宽 0.5ms

        /// 本秒最差(最小)同步余量，由 Timeline 每秒取走并复位。
        ///
        /// syncMarginUs 直方图是**整段累计**的, 取不出单秒分位数——第一版
        /// 逐秒发它的 p50, 跑出来是 25.5→22.5 的缓慢漂移, 那是整段均值被新
        /// 样本稀释, 不是这一秒的同步状况。
        ///
        /// 取最小值而不是均值: 余量的意义在于"离迟到还有多远", 一秒里有一帧
        /// 险些迟到, 均值完全看不出来, 而它正是丢帧的前兆。
        static constexpr int64_t kNoSyncSample = INT64_MAX;
        /// 时间线里"没测到"的统一哨兵值。不用 0：余量 0 是"即将开始丢帧"、
        /// 偏移 0 是"完美同步", 两者都是有意义的读数, 不能和"没测到"混同
        static constexpr double kNoSampleMs = -9999.0;
        std::atomic<int64_t> syncMarginWorstUs{kNoSyncSample};

        /// 同步余量的唯一写入口。软解(VEVideoDisplay)与硬解
        /// (VEMediaCodecVideoDecoder)两条路径各有一处上报, 收口在此是为了
        /// 避免"直方图记了、逐秒最差忘了记"这种一边漏账
        void noteSyncMargin(int64_t us) {
            syncMarginUs.add(us);
            int64_t prev = syncMarginWorstUs.load(std::memory_order_relaxed);
            while (us < prev && !syncMarginWorstUs.compare_exchange_weak(
                    prev, us, std::memory_order_relaxed)) {
                // prev 已被更新为当前值，循环重试
            }
        }

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
            syncMarginWorstUs.store(kNoSyncSample, std::memory_order_relaxed);
            audioQueuePeak.store(0, std::memory_order_relaxed);
            videoQueuePeak.store(0, std::memory_order_relaxed);
            frameQueuePeak.store(0, std::memory_order_relaxed);
            videoCreditPark.store(0, std::memory_order_relaxed);
            audioCreditPark.store(0, std::memory_order_relaxed);
            videoStarve.store(0, std::memory_order_relaxed);
            audioStarve.store(0, std::memory_order_relaxed);
            starveUs.reset();
            presentIntervalUs.reset();
            dropLate.store(0, std::memory_order_relaxed);
            dropOverflow.store(0, std::memory_order_relaxed);
            dropStale.store(0, std::memory_order_relaxed);
            dropSeekCatchup.store(0, std::memory_order_relaxed);
        }

        /// 逐秒时间线发射器。每秒往 logcat 打一条固定格式的 key=value 行。
        ///
        /// 为什么需要它：面板上的 p50/p95/累计计数是**整段播放的聚合值**，
        /// 看不出"某一秒全塌了"。而回退、抢占、热节流恰恰都是瞬时事件——
        /// 聚合之后它们被平均掉，正好是最该看见的那一类问题最看不见。
        ///
        /// 为什么打日志而不是走 JNI 上报：跑分报告需要的是离线可复算的原始
        /// 序列，日志天然带时间戳、能跨进程抓、崩溃后也还在。面板是给人看的
        /// 实时视图，两者不是一回事。
        ///
        /// 格式纪律(改动会破坏报告解析，等同改接口)：
        ///   - 固定前缀 VESTAT，一行一秒，字段一律 key=value 空格分隔
        ///   - **累计量一律发差值**(本秒新增)，绝对值靠外部累加即可还原；
        ///     反过来从绝对值求差要求采样无丢失，而日志恰恰会被配额丢
        ///   - 样本不足的分位数发 -1 而不是 0："没测到"和"零延迟"不能混
        ///   - 用 ALOGI：它是每秒 1 行的事件日志，不是每帧噪声，不该被
        ///     VE_TRACE_FRAME 那一档带走
        struct Timeline {
            void reset() { *this = Timeline(); }

            /// aq/vq/fq 为**瞬时**深度，-1 表示该轨不存在或无从得知。
            /// 由调用方从组件采样后传入，而不是让本类去持有组件指针——
            /// VEPerfStats 是纯数据容器，反过来依赖播放器组件会成环
            /// avOffsetUs 为调用方采到的**瞬时** A/V 偏移；
            /// 无视频轨时须传 kNoSyncSample——纯音频下 VEAVsync 的 lastDiff
            /// 从未被更新过, 直接取会得到一个随时钟单调发散的数(实测每秒
            /// 减 1000ms), 那不是 A/V 偏移, 是"没有 A/V 可比"
            void maybeEmit(VEPerfStats &s, int aq, int vq, int fq, int64_t avOffsetUs) {
                const int64_t now = nowUs();
                if (mLastUs == 0) {          // 首次调用只起锚，不发不完整的一秒
                    mLastUs = now;
                    snapshot(s);
                    return;
                }
                if (now - mLastUs < 1000000) {
                    return;
                }
                const double elapsed = static_cast<double>(now - mLastUs) / 1000000.0;
                const int64_t present = s.presentIntervalUs.count();
                // aq/vq/fq 发的是**瞬时深度**，不是 audioQueuePeak 那组
                // "只涨不落"的整段峰值。第一版误用了峰值，实测 vq 从 37 单调
                // 爬到 67，看起来像队列在这一秒涨了，其实只是历史最大值被刷新
                // 过——字段名承诺"这一秒"、实际给的是"到目前为止"。
                // 这个项目已经在同一类错误上栽过四次(硬解解码耗时实为背压、
                // 软解漏 send_packet、首帧上屏实为同步等待、丢帧只统计一类)。
                //
                // 取走并复位本秒最差余量。exchange 而非 load+store：
                // 两条解码路径都可能在写，读改写必须是原子的
                const int64_t worst = s.syncMarginWorstUs.exchange(
                        kNoSyncSample, std::memory_order_relaxed);
                // 这一秒一帧都没上屏时发 -9999 而不是 0：0 是"余量刚好归零"
                // (即将开始丢帧)，与"没测到"是相反的结论
                const double worstMs = (worst == kNoSyncSample)
                                       ? kNoSampleMs
                                       : static_cast<double>(worst) / 1000.0;
                ALOGI("VESTAT t=%d fps=%.1f dropLate=%" PRId64 " dropOvf=%" PRId64
                      " dropStale=%" PRId64 " dropSeek=%" PRId64
                      " vpark=%" PRId64 " apark=%" PRId64
                      " vstarve=%" PRId64 " astarve=%" PRId64
                      " aq=%d vq=%d fq=%d syncWorstMs=%.1f avOffMs=%.1f",
                      ++mSec,
                      static_cast<double>(present - mPresent) / elapsed,
                      s.dropLate.load(std::memory_order_relaxed) - mDropLate,
                      s.dropOverflow.load(std::memory_order_relaxed) - mDropOvf,
                      s.dropStale.load(std::memory_order_relaxed) - mDropStale,
                      s.dropSeekCatchup.load(std::memory_order_relaxed) - mDropSeek,
                      s.videoCreditPark.load(std::memory_order_relaxed) - mVPark,
                      s.audioCreditPark.load(std::memory_order_relaxed) - mAPark,
                      s.videoStarve.load(std::memory_order_relaxed) - mVStarve,
                      s.audioStarve.load(std::memory_order_relaxed) - mAStarve,
                      aq, vq, fq, worstMs,
                      avOffsetUs == kNoSyncSample
                              ? kNoSampleMs
                              : static_cast<double>(avOffsetUs) / 1000.0);
                mLastUs = now;
                snapshot(s);
            }

        private:
            void snapshot(const VEPerfStats &s) {
                mPresent = s.presentIntervalUs.count();
                mDropLate = s.dropLate.load(std::memory_order_relaxed);
                mDropOvf = s.dropOverflow.load(std::memory_order_relaxed);
                mDropStale = s.dropStale.load(std::memory_order_relaxed);
                mDropSeek = s.dropSeekCatchup.load(std::memory_order_relaxed);
                mVPark = s.videoCreditPark.load(std::memory_order_relaxed);
                mAPark = s.audioCreditPark.load(std::memory_order_relaxed);
                mVStarve = s.videoStarve.load(std::memory_order_relaxed);
                mAStarve = s.audioStarve.load(std::memory_order_relaxed);
            }

            int64_t mLastUs = 0;
            int mSec = 0;
            int64_t mPresent = 0, mDropLate = 0, mDropOvf = 0, mDropStale = 0,
                    mDropSeek = 0, mVPark = 0, mAPark = 0, mVStarve = 0, mAStarve = 0;
        };

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
            out += ",";
            starveUs.appendJson(out, "starveMs");
            out += ",";
            presentIntervalUs.appendJson(out, "presentIntervalMs");
            // 定长 buf 已经咬过三次(getStatsJson 的 768、这里的 128)：每次加
            // 字段都要重新估长度，而 snprintf 是**静默截断**，产出的残缺 JSON
            // 在上层表现为"面板忽然全空"，极难定位。
            // 改成逐字段追加到 std::string，长度不再是隐患。
            char buf[64];
            auto appendI64 = [&out, &buf](const char *key, long long v) {
                snprintf(buf, sizeof(buf), ",\"%s\":%lld", key, v);
                out += buf;
            };
            appendI64("audioQueuePeak", audioQueuePeak.load(std::memory_order_relaxed));
            appendI64("videoQueuePeak", videoQueuePeak.load(std::memory_order_relaxed));
            appendI64("frameQueuePeak", frameQueuePeak.load(std::memory_order_relaxed));
            appendI64("dropLate", dropLate.load(std::memory_order_relaxed));
            appendI64("dropOverflow", dropOverflow.load(std::memory_order_relaxed));
            appendI64("dropStale", dropStale.load(std::memory_order_relaxed));
            appendI64("dropSeekCatchup", dropSeekCatchup.load(std::memory_order_relaxed));
            appendI64("videoCreditPark", videoCreditPark.load(std::memory_order_relaxed));
            appendI64("audioCreditPark", audioCreditPark.load(std::memory_order_relaxed));
            appendI64("videoStarve", videoStarve.load(std::memory_order_relaxed));
            appendI64("audioStarve", audioStarve.load(std::memory_order_relaxed));
            return out;
        }
    };
}

#endif //LZPLAYER_VEPERFSTATS_H
