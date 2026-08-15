#ifndef LZPLAYER_VEPERFSTATS_H
#define LZPLAYER_VEPERFSTATS_H

#include <atomic>
#include <memory>
#include <string>

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include "VEPerfHistogram.h"
#include "Log.h"

namespace VE {

    /// 打点统一用 steady_clock。禁用 wall clock 的理由同 VEStartupTrace：
    /// NTP 校准会让差值出现负数
    inline int64_t nowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }


    /// 本线程已消耗的 CPU 时间(微秒)。与 nowUs() 的墙钟是两套量:
    /// 墙钟回答"花了多久"(含阻塞), CPU 回答"烧了多少算力"。
    /// 失败返回 0——调用方以差值使用, 0 只会让那一次失真, 不会累积
    inline int64_t threadCpuUs() {
        timespec ts{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
            return 0;
        }
        return static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
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

        /// 一条线程的"分段之和 vs 线程总量"自校验。
        ///
        /// 这一轮里指标出错七次, 全部是跑数据跑出来的、没有一次是审代码审出来
        /// 的。唯一能自动抓住这类问题的手段就是拿插桩区间之和跟一个独立测得的
        /// 总量对账 —— 启播里程碑一直有这条判据, 渲染刚补上, 其余环节还没有。
        ///
        /// threadUs 是线程累计 CPU(绝对值, 每次采样覆盖写),
        /// instrumentedUs 是插桩区间自身消耗的 CPU(增量累加)。
        /// 两者都是 CPU 时间, 每秒各取差值后相减 = 窗口外开销。
        ///
        /// 未插桩的环节(如 demux)instrumentedUs 恒为 0, 于是 gap 等于线程全部
        /// CPU —— 如实报出"这一环完全没有插桩", 而不是假装覆盖了。
        struct CpuGauge {
            std::atomic<int64_t> threadUs{0};
            std::atomic<int64_t> instrumentedUs{0};

            void reset() {
                threadUs.store(0, std::memory_order_relaxed);
                instrumentedUs.store(0, std::memory_order_relaxed);
            }
            /// 由所属线程调用: 刷新线程总量, 并累加本次区间消耗
            void note(int64_t nowCpuUs, int64_t spentUs) {
                threadUs.store(nowCpuUs, std::memory_order_relaxed);
                instrumentedUs.fetch_add(spentUs, std::memory_order_relaxed);
            }
            /// 仅刷新线程总量(用于无插桩区间的环节)
            void touch(int64_t nowCpuUs) {
                threadUs.store(nowCpuUs, std::memory_order_relaxed);
            }
        };

        CpuGauge vdecCpu;    ///< 视频解码线程
        CpuGauge adecCpu;    ///< 音频解码线程
        CpuGauge demuxCpu;   ///< 解封装线程(当前无插桩区间, gap 即全部)

        /// 渲染线程自身的累计 CPU 时间(CLOCK_THREAD_CPUTIME_ID, 微秒)。
        /// 由渲染线程每帧写入绝对值, Timeline 每秒取差值。
        ///
        /// 存在的意义是**自校验**: upload/draw/swap 三段之和若明显小于线程
        /// 实际 CPU, 说明渲染线程还有开销落在插桩窗口之外。首次测出来差额是
        /// 每帧 1.3ms(占 25%)——三段分解宣称覆盖了渲染成本, 实际漏掉四分之一,
        /// 而在加这条校验之前没有任何办法发现这件事。
        ///
        /// 启播里程碑有"分段之和与总耗时差 < 5ms"的判据, 渲染这边一直没有
        /// 对应的校验, 所以漏了 25% 无人察觉。
        std::atomic<int64_t> renderThreadCpuUs{0};
        /// 插桩区间(upload+draw+swap 所覆盖的那段)自身消耗的累计 CPU。
        /// 与 renderThreadCpuUs 同为 CPU 时间, 相减才是真实的窗口外开销
        std::atomic<int64_t> renderInstrumentedCpuUs{0};

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
            renderThreadCpuUs.store(0, std::memory_order_relaxed);
            renderInstrumentedCpuUs.store(0, std::memory_order_relaxed);
            vdecCpu.reset();
            adecCpu.reset();
            demuxCpu.reset();
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

        private:
            /// 进程 CPU 占用(单核归一, 100 = 吃满一个核)。读 /proc/self/stat 的
            /// utime+stime, 与 Kotlin 侧 sampleCpuPercent() 同一套读法。
            ///
            /// native 自己读而不是让 Java 把采样值喂下来: 跑分场景(intent 驱动、
            /// 无人看 UI)下时间线必须能独立成立, 依赖 UI 线程就意味着 UI 一卡
            /// 这一列就断。
            ///
            /// 首次调用只起基线返回 kNoSampleMs —— "没采到"与"占用为 0"不是
            /// 一回事, 而播放器占用 0 恰恰是个值得警觉的读数。
            double sampleCpuPercent(double elapsedSec) {
                FILE *fp = fopen("/proc/self/stat", "r");
                if (fp == nullptr) {
                    return kNoSampleMs;
                }
                char buf[512];
                const size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
                fclose(fp);
                if (n == 0) {
                    return kNoSampleMs;
                }
                buf[n] = '\0';
                // 必须从最后一个 ')' 之后开始数字段: comm 字段是进程名, 允许
                // 含空格和括号, 直接按空格切会错位
                const char *p = strrchr(buf, ')');
                if (p == nullptr) {
                    return kNoSampleMs;
                }
                ++p;
                // ') ' 之后是 state, utime 是其后第 11 个字段(stat 全表里第 14)
                long long utime = 0, stime = 0;
                int matched = 0;
                {
                    const char *q = p;
                    int field = 0;   // 0=state
                    while (*q != '\0') {
                        while (*q == ' ') { ++q; }
                        if (*q == '\0') { break; }
                        if (field == 11) { matched += sscanf(q, "%lld", &utime); }
                        if (field == 12) { matched += sscanf(q, "%lld", &stime); break; }
                        while (*q != ' ' && *q != '\0') { ++q; }
                        ++field;
                    }
                }
                if (matched < 2) {
                    return kNoSampleMs;
                }
                const long long total = utime + stime;
                if (mLastCpuTicks < 0) {          // 首次: 只起基线
                    mLastCpuTicks = total;
                    return kNoSampleMs;
                }
                const long hz = sysconf(_SC_CLK_TCK);
                const long long delta = total - mLastCpuTicks;
                mLastCpuTicks = total;
                if (hz <= 0 || elapsedSec <= 0) {
                    return kNoSampleMs;
                }
                return static_cast<double>(delta) / static_cast<double>(hz)
                       / elapsedSec * 100.0;
            }

        public:

            /// aq/vq/fq 为**瞬时**深度，-1 表示该轨不存在或无从得知。
            /// 由调用方从组件采样后传入，而不是让本类去持有组件指针——
            /// VEPerfStats 是纯数据容器，反过来依赖播放器组件会成环
            /// 曾有一个 avOffsetUs 参数, 已删除。
            ///
            /// 它取自 VEAVsync::getLastDiffUs(), 但 m_VideoPts 是在一帧处理的
            /// **开头**写入的(updateVideoPts → 判丢帧 → getWaitTime → 睡眠 →
            /// 渲染), 而本类从 player looper 在任意时刻采样, 绝大多数时候正落
            /// 在那段睡眠里。于是它量的是"距下一帧上屏还剩多久", 不是 A/V 偏移
            /// ——恒为正, 且随流水线状态变化, 实测新旧素材上分别是 72~75ms 与
            /// 20~26ms, 差异全来自流水线而非同步质量。
            ///
            /// syncWorstMs 没有这个问题: 它在 renderFrame 之前采样, 那时等待
            /// 已结束, 量的是真实余量。同步维度有它一个就够了。
            void maybeEmit(VEPerfStats &s, int aq, int vq, int fq) {
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
                      " aq=%d vq=%d fq=%d syncWorstMs=%.1f cpu=%.1f",
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
                      aq, vq, fq, worstMs, sampleCpuPercent(elapsed));
                // 渲染三段分解。单独一行而不是并进 VESTAT: 它只在软解路径
                // 有样本(硬解走 releaseOutputBuffer, 不经 GLES 渲染器),
                // 混在一起会让硬解那半行永远是哨兵。
                //
                // 存在的意义是判断 video_render 线程的 CPU 花在哪: upload 是
                // 真实的 CPU 拷贝, swap 理论上阻塞等 vsync **不该吃 CPU**——
                // 若线程 CPU 与 upload+draw 对不上, 差额就在 swap 的忙等里。
                double u50 = 0, u95 = 0, umax = 0, d50 = 0, d95 = 0, dmax = 0,
                       w50 = 0, w95 = 0, wmax = 0;
                if (s.uploadUs.percentiles(&u50, &u95, &umax)) {
                    s.drawUs.percentiles(&d50, &d95, &dmax);
                    s.swapUs.percentiles(&w50, &w95, &wmax);
                    // 每帧线程 CPU 与三段之和的差额。frames 用本秒上屏帧数,
                    // 不用固定帧率——掉帧时固定帧率会把差额算小。
                    //
                    // gapMs 两边都是 CPU 时间: 线程总 CPU 减去插桩区间自身的
                    // CPU。此前拿线程 CPU 直接减 u/d/w 三段墙钟, 单位就不对——
                    // swap 阻塞等 vsync 时墙钟远大于其 CPU, 减多了, 那个 gapMs
                    // 只是下界。现在它是量值, 可以直接读成"每帧有多少 CPU 花在
                    // 插桩窗口之外"。
                    //
                    // 三段仍保持墙钟: swap 的墙钟正是等 vsync 的时长, 换成 CPU
                    // 就把这个信息毁掉了。两套量各有各的问题域。
                    const int64_t cpuNow =
                            s.renderThreadCpuUs.load(std::memory_order_relaxed);
                    const int64_t frames = present - mPresent;
                    const int64_t instNow =
                            s.renderInstrumentedCpuUs.load(std::memory_order_relaxed);
                    double cpuMs = kNoSampleMs, inMs = kNoSampleMs, gapMs = kNoSampleMs;
                    if (mLastRenderCpuUs > 0 && frames > 0) {
                        const double f = static_cast<double>(frames);
                        cpuMs = static_cast<double>(cpuNow - mLastRenderCpuUs) / 1000.0 / f;
                        inMs = static_cast<double>(instNow - mLastInstCpuUs) / 1000.0 / f;
                        gapMs = cpuMs - inMs;
                    }
                    mLastRenderCpuUs = cpuNow;
                    mLastInstCpuUs = instNow;
                    ALOGI("VERENDER t=%d uploadMs=%.2f drawMs=%.2f swapMs=%.2f "
                          "threadCpuMs=%.2f inCpuMs=%.2f gapMs=%.2f",
                          mSec, u50, d50, w50, cpuMs, inMs, gapMs);
                }
                // 三条线程的自校验。单独一行, 与 VERENDER 同构。
                // gap 大 = 该环节的开销大部分没被任何指标覆盖, 出问题时
                // 现有指标不会有任何反应 —— 这正是最该先补插桩的地方
                emitGauge("vdec", s.vdecCpu, mVdec, elapsed);
                emitGauge("adec", s.adecCpu, mAdec, elapsed);
                emitGauge("demux", s.demuxCpu, mDemux, elapsed);

                mLastUs = now;
                snapshot(s);
            }

        private:
            struct GaugePrev { int64_t thread = 0, inst = 0; bool armed = false; };

            /// 单位一律 %(单核归一), 不用 ms/帧: demux 与解码的"每帧"含义
            /// 不同(一次读可能出多个包), 换算成占用率才能横向比较
            void emitGauge(const char *name, const CpuGauge &g,
                           GaugePrev &prev, double elapsedSec) {
                const int64_t t = g.threadUs.load(std::memory_order_relaxed);
                const int64_t i = g.instrumentedUs.load(std::memory_order_relaxed);
                if (!prev.armed) {          // 首次只起基线
                    prev.thread = t; prev.inst = i; prev.armed = true;
                    return;
                }
                if (t == prev.thread) {     // 该线程这一秒没跑过, 不发空行
                    return;
                }
                const double us = elapsedSec * 10000.0;   // 1% = elapsed*10000us
                const double cpu = static_cast<double>(t - prev.thread) / us;
                const double inst = static_cast<double>(i - prev.inst) / us;
                prev.thread = t; prev.inst = i;
                ALOGI("VEGAUGE t=%d who=%s cpu=%.1f instrumented=%.1f gap=%.1f",
                      mSec, name, cpu, inst, cpu - inst);
            }

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
            /// -1 = 还没起基线
            long long mLastCpuTicks = -1;
            int64_t mLastRenderCpuUs = 0;
            int64_t mLastInstCpuUs = 0;
            GaugePrev mVdec, mAdec, mDemux;
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
