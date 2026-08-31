#ifndef LZPLAYER_VESEEKTRACE_H
#define LZPLAYER_VESEEKTRACE_H

#include <cstdio>
#include <mutex>
#include <string>

#include "VEPerfStats.h"   // nowUs()
#include "Log.h"

namespace VE {

    /// seek 链路追踪：三阶段耗时 + 精度，环形缓冲保留最近 kCapacity 次。
    ///
    /// 与启播里程碑分开而不是塞进同一个类：seek 会发生很多次，是"一串记录"；
    /// 启播一次播放只有一次，是"一份快照"。混在一起会让两边的 reset 语义打架。
    ///
    /// 三阶段与 VEPlayer 的 SEEK_STAGE_* 一一对应：
    ///   暂停(PAUSING) 让组件停止消费 → 定位(SEEKING) demux 跳转 + 解码器 flush
    ///   → 预热(PRIMING) 重启管线并等首帧上屏
    /// **定位段偏大**通常是关键帧间距或 demux 回溯的问题；
    /// **预热段偏大**是解码器追帧的问题。分开看才知道该动哪边。
    ///
    /// 写入只发生在 player looper，读取来自 JNI 线程，故全程加锁——
    /// 一次 seek 只写四五次，不是热路径。
    class VESeekTrace {
    public:
        static constexpr int kCapacity = 10;
        /// 第三段"无样本"哨兵。**不能用 0 顶替** —— 0 在报告里与"瞬间完成"
        /// 长得一模一样, 这正是本项目反复栽的那类"没测到被当成好结果"。
        static constexpr double kNoStageMs = -9999.0;

        /// kind / paramKey 只影响 toJson 的**输出形状**，计时逻辑完全不变。
        ///
        /// 默认值让既有的 seek 实例保持原样(只多一个 "kind" 键，解析方忽略
        /// 未知键)，变速与切轨复用同一套三阶段与环形缓冲 ——
        /// perf-metrics 关键约束 12：不另造容器。
        ///
        /// 三段在各 kind 下的含义(**唯一口径, 改这里就要改报告**)：
        ///   seek  : 暂停组件 / demux 定位+flush / 预热到首帧上屏
        ///   speed : 请求排队 / 生效(时钟+音频变速器+字幕重排) / —— 见下
        ///   track : 请求排队 / 装配(换流+解码器重建) / 全链追平到首帧上屏
        explicit VESeekTrace(const char *kind = "seek", const char *paramKey = nullptr)
                : mKind(kind), mParamKey(paramKey) {}

        void reset() {
            std::lock_guard<std::mutex> lk(mMutex);
            mCount = 0;
            mNext = 0;
            mPending = Record();
            mInFlight = false;
        }

        /// 一次 seek 开始。requestedMs 是请求位置
        void begin(double requestedMs, bool hardware) {
            std::lock_guard<std::mutex> lk(mMutex);
            mPending = Record();
            mPending.requestedMs = requestedMs;
            mPending.hardware = hardware;
            mPending.beginUs = nowUs();
            mPending.stageBeginUs = mPending.beginUs;
            mInFlight = true;
        }

        /// 本次事件的领域参数(变速=目标速率, 切轨=目标轨道号)。
        /// 须紧跟 begin() 调用；无 paramKey 的实例(seek)不会输出它。
        void setParam(double v) {
            std::lock_guard<std::mutex> lk(mMutex);
            if (mInFlight) {
                mPending.param = v;
                mPending.hasParam = true;
            }
        }

        /// 事后补写"请求位置"。切轨用：请求发出时(JNI 线程)还读不到续播锚点，
        /// 锚点要到 switchAudioTrack 里算出 resumeMs 才有 —— 精度的口径因此是
        /// **切轨后首帧 pts − 续播锚点**，而不是"减去某个用户请求的位置"。
        void setRequested(double ms) {
            std::lock_guard<std::mutex> lk(mMutex);
            if (mInFlight) {
                mPending.requestedMs = ms;
            }
        }

        /// 阶段①结束、②开始
        void endPausing() { closeStage(&Record::pausingMs); }

        /// 阶段②结束、③开始
        void endSeeking() { closeStage(&Record::seekingMs); }

        /// 阶段③结束：首帧已上屏。actualPtsUs 为该帧的实际 pts，用于算精度
        /// actualPtsUs < 0 表示"没有首帧可比对"(无视频轨)：照常结算三阶段
        /// 耗时，但**不产生精度值**——造一个 0 出来会被当成"精度完美"
        void endPriming(int64_t actualPtsUs) {
            std::lock_guard<std::mutex> lk(mMutex);
            if (!mInFlight) {
                return;
            }
            const int64_t now = nowUs();
            mPending.primingMs = static_cast<double>(now - mPending.stageBeginUs) / 1000.0;
            mPending.totalMs = static_cast<double>(now - mPending.beginUs) / 1000.0;
            // 2026-09-01 结案：这个值**可信**。此前 2026-08-16 记的"不可信"
            // 已被实测推翻，那条判断的两个依据都不成立：
            //
            //   ① "硬解恒为 0.0 → 回传的是请求值本身" —— 不成立。用 23.976fps
            //      素材(帧间隔 41.7083ms)、请求位置刻意避开帧栅格重测 10 次，
            //      硬解 10/10 精确落在"请求位置之后的第一个真实帧"上
            //      (落点 0.15~0.88 帧)。回传的是真实首帧 pts。此前恒为 0 只是
            //      因为那个素材的请求位置**恰好都落在帧栅格上**。
            //   ② "从关键帧起播只可能落在请求之前(负值)" —— 前提就是错的。
            //      精准 seek 的实现是"定位到关键帧 → 解码 → 丢弃到目标"，
            //      首帧因此必然落在目标**之后**，非负才是对的。
            //
            // 同一轮实测顺带定位并修掉一个真 bug：软解 7/10 比硬解恰好晚一帧
            // (追赶期 AVDISCARD_NONREF 会把身为 B 帧的目标帧本身跳掉)，
            // 见 VEVideoDecoder 送包处的注释。修后软解落点均值 0.46 帧，
            // 与硬解基准一致。
            //
            // 精度 = 首帧实际位置 − 请求位置。**符号有意义**：正=落在请求点
            // 之后(精准 seek 的常态，量值应在 [0, 一个帧间隔) 内)；
            // 负或超过一帧都说明追帧边界出了问题。
            // 判据已进 gen-report.py 的「seek 精度落在帧栅格上」，
            // 它同时校验区间与"是否落在素材的真实帧栅格上"——
            // 后者才是识破"把请求值原样回传"的那一条。
            if (actualPtsUs >= 0) {
                mPending.accuracyMs =
                        static_cast<double>(actualPtsUs) / 1000.0 - mPending.requestedMs;
                mPending.hasAccuracy = true;
            }
            commitLocked();
        }

        /// 只有两段的事件在此结算：关掉第②段，第③段记为**无样本**。
        ///
        /// 变速走这条路: 它的第三段本该是"新速率下第一帧上屏", 但 VEPlayer
        /// **收不到逐帧信号** —— FIRST_FRAME 只在 seek 预热期发, 位置回调是
        /// player looper 上的定时 tick(量化到 tick 间隔)。为此新增一条逐帧
        /// 通知会直接违反关键约束 1(测量不得影响被测对象), 拿 tick 顶替则是
        /// 把量化误差当成测量值。所以宁可留空, 也不造一个数出来。
        void endAtStage2() {
            std::lock_guard<std::mutex> lk(mMutex);
            if (!mInFlight) {
                return;
            }
            const int64_t now = nowUs();
            mPending.seekingMs = static_cast<double>(now - mPending.stageBeginUs) / 1000.0;
            mPending.primingMs = kNoStageMs;
            mPending.totalMs = static_cast<double>(now - mPending.beginUs) / 1000.0;
            commitLocked();
        }

        /// seek 被中途放弃(换源/stop/新的 seek 抢占)。仍然入库——
        /// 被打断的那些同样是有用样本，看得出卡在哪个阶段
        void abort() {
            std::lock_guard<std::mutex> lk(mMutex);
            if (!mInFlight) {
                return;
            }
            mPending.aborted = true;
            mPending.totalMs =
                    static_cast<double>(nowUs() - mPending.beginUs) / 1000.0;
            commitLocked();
        }

        std::string toJson() const {
            std::lock_guard<std::mutex> lk(mMutex);
            std::string out = "{\"count\":";
            char head[96];
            snprintf(head, sizeof(head), "%d,\"kind\":\"%s\",\"items\":[",
                     mCount, mKind);
            out += head;
            // 从最近一次往回列，UI 不必再排序
            for (int i = 0; i < mCount; ++i) {
                const int idx = (mNext - 1 - i + kCapacity * 2) % kCapacity;
                const Record &r = mItems[idx];
                // 无样本的第三段输出 null 而不是 -9999：与 accuracyMs 同一套
                // 三态约定(null=没测 / 数值=测到)，解析方不必认第二个哨兵
                char priming[32];
                if (r.primingMs == kNoStageMs) {
                    snprintf(priming, sizeof(priming), "null");
                } else {
                    snprintf(priming, sizeof(priming), "%.1f", r.primingMs);
                }
                char param[64] = "";
                if (mParamKey != nullptr && r.hasParam) {
                    snprintf(param, sizeof(param), ",\"%s\":%.3f", mParamKey, r.param);
                }
                char buf[400];
                snprintf(buf, sizeof(buf),
                         "%s{\"requestedMs\":%.1f,\"pausingMs\":%.1f,"
                         "\"seekingMs\":%.1f,\"primingMs\":%s,\"totalMs\":%.1f,"
                         "\"accuracyMs\":%s,\"decodePath\":\"%s\",\"aborted\":%s%s}",
                         i == 0 ? "" : ",",
                         r.requestedMs, r.pausingMs, r.seekingMs, priming,
                         r.totalMs,
                         r.hasAccuracy ? accuracyStr(r).c_str() : "null",
                         r.hardware ? "hardware" : "software",
                         r.aborted ? "true" : "false", param);
                out += buf;
            }
            out += "]}";
            return out;
        }

    private:
        struct Record {
            double requestedMs = -1;
            double pausingMs = -1;
            double seekingMs = -1;
            double primingMs = -1;
            double totalMs = -1;
            double accuracyMs = 0;
            bool hasAccuracy = false;
            bool aborted = false;
            bool hardware = false;
            double param = 0;
            bool hasParam = false;
            int64_t beginUs = 0;
            int64_t stageBeginUs = 0;
        };

        static std::string accuracyStr(const Record &r) {
            char b[32];
            snprintf(b, sizeof(b), "%.1f", r.accuracyMs);
            return std::string(b);
        }

        void closeStage(double Record::*field) {
            std::lock_guard<std::mutex> lk(mMutex);
            if (!mInFlight) {
                return;
            }
            const int64_t now = nowUs();
            mPending.*field = static_cast<double>(now - mPending.stageBeginUs) / 1000.0;
            mPending.stageBeginUs = now;
        }

        /// 调用方需已持锁
        void commitLocked() {
            mItems[mNext] = mPending;
            mNext = (mNext + 1) % kCapacity;
            if (mCount < kCapacity) {
                ++mCount;
            }
            mInFlight = false;
        }

        mutable std::mutex mMutex;
        Record mItems[kCapacity];
        Record mPending;
        int mNext = 0;
        int mCount = 0;
        bool mInFlight = false;
        const char *mKind = "seek";
        const char *mParamKey = nullptr;
    };
}

#endif //LZPLAYER_VESEEKTRACE_H
