#ifndef LZPLAYER_VEPERFHISTOGRAM_H
#define LZPLAYER_VEPERFHISTOGRAM_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace VE {

    /// 每帧耗时的分位数统计。**热路径专用，与 VEStartupTrace 的取舍完全不同**。
    ///
    /// VEStartupTrace 全程持 mutex，因为它一次启播只被调十几次。这里每秒被调
    /// 几十到上百次(每帧 1~3 次)，加锁会引入真实的锁竞争与 cache 行争用——
    /// 测量本身就会拖慢被测对象，那测出来的数字也就没有意义了。
    ///
    /// 因此设计成**单写者无锁**：每个实例只由所属组件的 looper 写入，
    /// 读取侧(JNI 线程)允许读到"正在被更新的桶计数"。桶计数用 relaxed 原子，
    /// 既避免了数据竞争的未定义行为，在 ARM 上又几乎零成本(普通 load/store)。
    /// 分位数本身容忍这种量级的误差——它不是用来做精确断言的，
    /// 是用来看趋势和找瓶颈的。
    ///
    /// 只回传 p50/p95/max + 样本数，**不跨 JNI 传原始样本**：60fps 播一分钟
    /// 就是 3600 条，光是搬运就会影响帧率。
    class VEPerfHistogram {
    public:
        /// minUs   第一个桶的下界(可为负，同步余量会是负数)
        /// bucketUs 桶宽
        /// buckets  桶数。超出上界的样本落进溢出桶，仍计入 count 与 max
        VEPerfHistogram(int64_t minUs, int64_t bucketUs, int buckets);

        /// 记一个样本。只允许所属 looper 调用
        void add(int64_t valueUs);

        /// seek / 换源 / flush 时清零。否则一次 seek 的抖动会污染整段读数
        void reset();

        int64_t count() const { return mCount.load(std::memory_order_relaxed); }

        /// 分位数。样本数不足(< kMinSamples)时返回 false，调用方应显示 "--"
        /// 而不是拿一个由三五个样本算出来的数字当结论。
        bool percentiles(double *p50Ms, double *p95Ms, double *maxMs) const;

        /// 追加形如 "\"name\":{\"p50\":..,\"p95\":..,\"max\":..,\"n\":..}"
        /// 的片段；样本不足时三个分位数输出 -1。
        void appendJson(std::string &out, const char *name) const;

        /// 低于这个样本数不给分位数
        static constexpr int64_t kMinSamples = 30;

    private:
        /// 桶下界对应的值
        const int64_t mMinUs;
        const int64_t mBucketUs;
        /// 溢出桶不含在内，实际数组长度是 mBuckets + 1
        const int mBuckets;

        std::vector<std::atomic<uint32_t>> mCounts;
        std::atomic<int64_t> mCount;
        /// 真实极值，不受分桶精度影响
        std::atomic<int64_t> mMaxUs;
        std::atomic<int64_t> mMinObservedUs;

        /// 从累计分布里取分位点，返回该桶的上界(微秒)
        int64_t quantileUs(int64_t target) const;
    };
}

#endif //LZPLAYER_VEPERFHISTOGRAM_H
