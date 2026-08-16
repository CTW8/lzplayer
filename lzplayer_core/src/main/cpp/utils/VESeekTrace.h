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
            // ⚠ 2026-08-16 实测: 这个值目前**不可信**, 两条路径各自坏在不同
            // 地方, 修好前不要拿它下结论。
            //   素材 base-h264-1080p 关键帧在 0/1.967/3.933/5.900s(ffprobe),
            //   请求 2.000s→应为 -33.3, 请求 6.000s→应为 -100。
            //   实测硬解两次都恰为 0.0 —— 落在关键帧上不可能为 0, 说明回传的
            //     pts 就是请求值本身;
            //   实测软解两次都恰为 +33.3 —— 符号为正, 而从关键帧起播只可能落在
            //     请求位置之前(负值), 且第二次完全不匹配。
            // 待查: endPriming(pts) 传进来的 pts 究竟取自哪一帧。
            //
            // 精度 = 首帧实际位置 − 请求位置。**符号有意义**：
            // 负=落在请求点之前(demux 只能定位到关键帧时的常态)
            // 精度对不上 ffprobe 关键帧, 且两条路径坏法不同。先把原值打出来:
            // 单位(us/ms)、来源(真首帧 pts / 缺省 0)一次就能看清, 比读代码可靠
            ALOGW("VESeekTrace::endPriming actualPtsUs=%lld requestedMs=%.1f hw=%d",
                  (long long) actualPtsUs, mPending.requestedMs,
                  (int) mPending.hardware);
            if (actualPtsUs >= 0) {
                mPending.accuracyMs =
                        static_cast<double>(actualPtsUs) / 1000.0 - mPending.requestedMs;
                mPending.hasAccuracy = true;
            }
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
            char head[64];
            snprintf(head, sizeof(head), "%d,\"items\":[", mCount);
            out += head;
            // 从最近一次往回列，UI 不必再排序
            for (int i = 0; i < mCount; ++i) {
                const int idx = (mNext - 1 - i + kCapacity * 2) % kCapacity;
                const Record &r = mItems[idx];
                char buf[320];
                snprintf(buf, sizeof(buf),
                         "%s{\"requestedMs\":%.1f,\"pausingMs\":%.1f,"
                         "\"seekingMs\":%.1f,\"primingMs\":%.1f,\"totalMs\":%.1f,"
                         "\"accuracyMs\":%s,\"decodePath\":\"%s\",\"aborted\":%s}",
                         i == 0 ? "" : ",",
                         r.requestedMs, r.pausingMs, r.seekingMs, r.primingMs,
                         r.totalMs,
                         r.hasAccuracy ? accuracyStr(r).c_str() : "null",
                         r.hardware ? "hardware" : "software",
                         r.aborted ? "true" : "false");
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
    };
}

#endif //LZPLAYER_VESEEKTRACE_H
