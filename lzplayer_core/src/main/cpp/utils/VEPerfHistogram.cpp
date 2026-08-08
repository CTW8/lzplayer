#include "VEPerfHistogram.h"

#include <cstdio>
#include <limits>

namespace VE {

    VEPerfHistogram::VEPerfHistogram(int64_t minUs, int64_t bucketUs, int buckets)
            : mMinUs(minUs),
              mBucketUs(bucketUs > 0 ? bucketUs : 1),
              mBuckets(buckets > 0 ? buckets : 1),
              mCounts(static_cast<size_t>(buckets > 0 ? buckets : 1) + 1),
              mCount(0),
              mMaxUs(std::numeric_limits<int64_t>::min()),
              mMinObservedUs(std::numeric_limits<int64_t>::max()) {
        reset();
    }

    void VEPerfHistogram::reset() {
        for (auto &c : mCounts) {
            c.store(0, std::memory_order_relaxed);
        }
        mCount.store(0, std::memory_order_relaxed);
        mMaxUs.store(std::numeric_limits<int64_t>::min(), std::memory_order_relaxed);
        mMinObservedUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_relaxed);
    }

    void VEPerfHistogram::add(int64_t valueUs) {
        // 落桶。低于下界的归 0 号桶，高于上界的归溢出桶——两端都不丢样本，
        // 否则 count 与分位数会对不上
        int idx;
        if (valueUs < mMinUs) {
            idx = 0;
        } else {
            const int64_t k = (valueUs - mMinUs) / mBucketUs;
            idx = (k >= mBuckets) ? mBuckets : static_cast<int>(k);
        }
        mCounts[static_cast<size_t>(idx)].fetch_add(1, std::memory_order_relaxed);
        mCount.fetch_add(1, std::memory_order_relaxed);

        // 极值单独记真实值，不受桶宽精度影响
        int64_t prevMax = mMaxUs.load(std::memory_order_relaxed);
        if (valueUs > prevMax) {
            mMaxUs.store(valueUs, std::memory_order_relaxed);
        }
        int64_t prevMin = mMinObservedUs.load(std::memory_order_relaxed);
        if (valueUs < prevMin) {
            mMinObservedUs.store(valueUs, std::memory_order_relaxed);
        }
    }

    int64_t VEPerfHistogram::quantileUs(int64_t target) const {
        int64_t acc = 0;
        for (int i = 0; i <= mBuckets; ++i) {
            acc += mCounts[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            if (acc >= target) {
                if (i >= mBuckets) {
                    // 落在溢出桶：真实值只知道"至少是上界"，用观测到的 max 更诚实
                    return mMaxUs.load(std::memory_order_relaxed);
                }
                // 取桶上界，宁可略高不要略低——分位数用来判"最坏情况有多坏"，
                // 报低了会掩盖问题
                return mMinUs + static_cast<int64_t>(i + 1) * mBucketUs;
            }
        }
        return mMaxUs.load(std::memory_order_relaxed);
    }

    bool VEPerfHistogram::percentiles(double *p50Ms, double *p95Ms, double *maxMs) const {
        const int64_t n = mCount.load(std::memory_order_relaxed);
        if (n < kMinSamples) {
            return false;
        }
        // 分位点位置用向上取整，n=30 时 p95 落在第 29 个样本上
        const int64_t t50 = (n + 1) / 2;
        const int64_t t95 = (n * 95 + 99) / 100;
        if (p50Ms) *p50Ms = static_cast<double>(quantileUs(t50)) / 1000.0;
        if (p95Ms) *p95Ms = static_cast<double>(quantileUs(t95)) / 1000.0;
        if (maxMs) *maxMs = static_cast<double>(mMaxUs.load(std::memory_order_relaxed)) / 1000.0;
        return true;
    }

    void VEPerfHistogram::appendJson(std::string &out, const char *name) const {
        double p50 = 0, p95 = 0, mx = 0;
        const bool ok = percentiles(&p50, &p95, &mx);
        const int64_t n = mCount.load(std::memory_order_relaxed);
        char buf[192];
        if (ok) {
            snprintf(buf, sizeof(buf),
                     "\"%s\":{\"p50\":%.2f,\"p95\":%.2f,\"max\":%.2f,\"n\":%lld}",
                     name, p50, p95, mx, static_cast<long long>(n));
        } else {
            // 样本不足一律给 -1。给个"看着像真的"的数字比不给更有害——
            // 三五个样本算出来的 p95 会被当成结论
            snprintf(buf, sizeof(buf),
                     "\"%s\":{\"p50\":-1,\"p95\":-1,\"max\":-1,\"n\":%lld}",
                     name, static_cast<long long>(n));
        }
        out += buf;
    }
}
