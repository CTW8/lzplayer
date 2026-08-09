#include "VEStartupTrace.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace VE {

    namespace {
        int64_t nowNs() {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        /// 往 JSON 里追加一个毫秒字段。缺失值原样输出 -1，不做美化——
        /// 上层据此显示 "--"
        /// NaN → JSON null(没采到)；其余原样输出，含负数(采集点顺序颠倒)。
        /// 不再用 -1 兼表两种状态——-1 本身是个合法时长，混用就分不出来了
        void appendMs(std::string &out, const char *key, double ms, bool comma = true) {
            char buf[96];
            if (std::isnan(ms)) {
                snprintf(buf, sizeof(buf), "\"%s\":null%s", key, comma ? "," : "");
            } else {
                snprintf(buf, sizeof(buf), "\"%s\":%.1f%s", key, ms, comma ? "," : "");
            }
            out += buf;
        }
    }

    VEStartupTrace::VEStartupTrace() {
        reset();
    }

    void VEStartupTrace::reset() {
        std::lock_guard<std::mutex> lk(mMutex);
        for (int i = 0; i < kMilestoneCount; ++i) {
            mAt[i] = -1;
        }
        mHardware = false;
        mHasDecodePath = false;
        mFirstFrameWaitUs = -1;
        mHasFirstFrameWait = false;
    }

    void VEStartupTrace::mark(Milestone m) {
        if (m < 0 || m >= kMilestoneCount) {
            return;
        }
        std::lock_guard<std::mutex> lk(mMutex);
        if (mAt[m] < 0) {
            mAt[m] = nowNs();
        }
    }

    bool VEStartupTrace::marked(Milestone m) const {
        if (m < 0 || m >= kMilestoneCount) {
            return false;
        }
        std::lock_guard<std::mutex> lk(mMutex);
        return mAt[m] >= 0;
    }

    void VEStartupTrace::markIfArmed(Milestone gate, Milestone m) {
        if (gate < 0 || gate >= kMilestoneCount || m < 0 || m >= kMilestoneCount) {
            return;
        }
        std::lock_guard<std::mutex> lk(mMutex);
        if (mAt[gate] < 0) {
            return;   // 门还没开，这次执行与目标事件无关
        }
        if (mAt[m] < 0) {
            mAt[m] = nowNs();
        }
    }

    void VEStartupTrace::setFirstFrameWaitUs(int64_t waitUs) {
        std::lock_guard<std::mutex> lk(mMutex);
        if (!mHasFirstFrameWait) {
            mFirstFrameWaitUs = waitUs;
            mHasFirstFrameWait = true;
        }
    }

    void VEStartupTrace::setDecodePath(bool hardware) {
        std::lock_guard<std::mutex> lk(mMutex);
        mHardware = hardware;
        mHasDecodePath = true;
    }

    double VEStartupTrace::spanMs(Milestone from, Milestone to) const {
        if (mAt[from] < 0 || mAt[to] < 0) {
            return std::nan("");
        }
        return static_cast<double>(mAt[to] - mAt[from]) / 1e6;
    }

    double VEStartupTrace::offsetMs(Milestone m) const {
        return spanMs(T0_REQUEST, m);
    }

    std::string VEStartupTrace::toJson() const {
        std::lock_guard<std::mutex> lk(mMutex);

        // 分段定义。这些段首尾相接、恰好覆盖 T0→T4 与 T5→T7 两区间，
        // 因此它们之和严格等于 startupTotalMs(整数纳秒相减，无累积误差)。
        // configure 不在其列——它是一条跨段的叠加区间，见下方说明。
        const double queueWait   = spanMs(T0_REQUEST, T0_DISPATCH);
        const double open        = spanMs(T0_DISPATCH, T1_CONTAINER_OPEN);
        const double streamInfo  = spanMs(T1_CONTAINER_OPEN, T2_STREAM_INFO);
        const double trackList   = spanMs(T2_STREAM_INFO, T3_TRACKS_READY);
        const double buildChain  = spanMs(T3_TRACKS_READY, T4_CHAIN_READY);
        const double configure   = spanMs(T4A_CONFIGURE_BEGIN, T4A_CONFIGURE_END);
        // configure 内部三段(仅硬解)：创建 / 配置 / 启动
        const double codecCreate = spanMs(T4A_CONFIGURE_BEGIN, T4A_CODEC_CREATED);
        const double codecConfig = spanMs(T4A_CODEC_CREATED, T4A_CODEC_CONFIGURED);
        const double codecStart  = spanMs(T4A_CODEC_CONFIGURED, T4A_CONFIGURE_END);
        const double firstDecode = spanMs(T5_START, T6_FIRST_FRAME_DECODED);
        const double firstPresent= spanMs(T6_FIRST_FRAME_DECODED, T7_FIRST_FRAME_PRESENTED);

        // configure **不嵌在建链段里**。解码器的 prepare 是异步的(投消息进
        // 自己的 looper)，continuePrepare 在 configure 真正执行之前就返回了，
        // 所以 T3→T4 只是"接线"耗时(实测 0.3ms)，而 configure 那几十到几百毫秒
        // 落在 start 之后的首帧窗口里。
        //
        // 早先按"configure 嵌在建链内"算 buildChain − configure，会得到负数。
        // 正确做法是把 configure 当作一条**叠加区间**单独呈现(它自带
        // t4aBegin/t4aEnd 两个绝对位置)，不去和任何分段做减法。
        const bool configureOverlapsFirstFrame =
                (mAt[T4A_CONFIGURE_END] >= 0 && mAt[T5_START] >= 0 &&
                 mAt[T4A_CONFIGURE_END] > mAt[T5_START]);

        const double prepareTotal = spanMs(T0_REQUEST, T4_CHAIN_READY);
        const double firstFrame   = spanMs(T5_START, T7_FIRST_FRAME_PRESENTED);
        const double firstSound   = spanMs(T5_START, T8_FIRST_AUDIO);

        // T4→T5 单独列出且**不计入启播总耗时**：非自动起播时这段是用户的
        // 思考时间，把它算进去会让"启播耗时"变成一个毫无意义的数字。
        const double startGap = spanMs(T4_CHAIN_READY, T5_START);

        // 启播总耗时 = 就绪耗时 + 首帧耗时。纯音频文件没有 T6/T7，
        // 退化为"到听见声音"，否则这类文件永远拿不到总耗时。
        double startupTotal = -1;
        const char *totalBasis = "none";
        if (prepareTotal >= 0 && firstFrame >= 0) {
            startupTotal = prepareTotal + firstFrame;
            totalBasis = "video";
        } else if (prepareTotal >= 0 && firstSound >= 0) {
            startupTotal = prepareTotal + firstSound;
            totalBasis = "audio";
        }

        std::string out;
        out.reserve(1024);
        out += "{";

        // valid 的门槛是"prepare 走完了"，不要求首帧——起播失败的那次数据
        // 同样有价值(能看出卡在哪一段)，把它判为无效反而丢掉了最该看的样本
        out += (prepareTotal >= 0) ? "\"valid\":true," : "\"valid\":false,";

        if (mHasDecodePath) {
            // 硬解与软解的 T7 物理含义不同：软解是"提交给 GLES/Vulkan 并 swap"，
            // 硬解是"交给 SurfaceFlinger 合成"，两者都不等于像素真正点亮。
            // 因此**跨解码路径对照首帧耗时会得出错误结论**，必须带上这个字段
            // 让上层能在报告里加脚注。
            out += mHardware ? "\"decodePath\":\"hardware\"," : "\"decodePath\":\"software\",";
        } else {
            out += "\"decodePath\":\"-\",";
        }
        out += "\"totalBasis\":\"";
        out += totalBasis;
        out += "\",";

        appendMs(out, "queueWaitMs", queueWait);
        appendMs(out, "containerOpenMs", open);
        appendMs(out, "streamInfoMs", streamInfo);
        appendMs(out, "trackListMs", trackList);
        appendMs(out, "buildChainMs", buildChain);
        appendMs(out, "decoderConfigureMs", configure);
        appendMs(out, "codecCreateMs", codecCreate);
        appendMs(out, "codecConfigureMs", codecConfig);
        appendMs(out, "codecStartMs", codecStart);
        out += configureOverlapsFirstFrame
               ? "\"configureOverlapsFirstFrame\":true,"
               : "\"configureOverlapsFirstFrame\":false,";
        appendMs(out, "startGapMs", startGap);
        // 首帧刻意拆两段：T5→T6 是解码，T6→T7 是等同步时钟。两者的优化方向
        // 完全相反(前者加快解码，后者动起播锚点策略)，合成一个数字会把人
        // 引向错误方向，所以这里绝不提供"合成版"之外的唯一口径。
        appendMs(out, "firstFrameDecodeMs", firstDecode);
        appendMs(out, "firstFramePresentMs", firstPresent);
        // T6→T7 的内部拆分
        appendMs(out, "ffQueueHopMs", spanMs(T6_FIRST_FRAME_DECODED, T6B_FRAME_QUEUED));
        appendMs(out, "ffSyncEnterMs", spanMs(T6B_FRAME_QUEUED, T6C_SYNC_ENTER));
        appendMs(out, "ffRenderEnterMs", spanMs(T6C_SYNC_ENTER, T6D_RENDER_ENTER));
        appendMs(out, "ffRenderMs", spanMs(T6D_RENDER_ENTER, T7_FIRST_FRAME_PRESENTED));
        appendMs(out, "ffSyncWaitMs",
                 mHasFirstFrameWait ? static_cast<double>(mFirstFrameWaitUs) / 1000.0
                                    : std::nan(""));
        // 子分段自洽校验：四段之和应等于父段(首帧上屏)。
        // 顶层分段一直有这条校验，子分段没有——T6C 记错了因此无人发现。
        // 规则：任何分段一旦拆出子分段，必须同时给出与父段的差值。
        const double ffSum = spanMs(T6_FIRST_FRAME_DECODED, T6B_FRAME_QUEUED) +
                             spanMs(T6B_FRAME_QUEUED, T6C_SYNC_ENTER) +
                             spanMs(T6C_SYNC_ENTER, T6D_RENDER_ENTER) +
                             spanMs(T6D_RENDER_ENTER, T7_FIRST_FRAME_PRESENTED);
        appendMs(out, "ffSumDeltaMs", ffSum - firstPresent);
        appendMs(out, "prepareTotalMs", prepareTotal);
        appendMs(out, "firstFrameMs", firstFrame);
        appendMs(out, "firstSoundMs", firstSound);
        appendMs(out, "startupTotalMs", startupTotal);

        // 原始里程碑(相对 T0 的偏移)，用于校验分段是否漏了空隙
        out += "\"marks\":{";
        static const char *kKeys[kMilestoneCount] = {
                "t0", "t0d", "t1", "t2", "t3", "t4aBegin",
                "t4aCreated", "t4aConfigured", "t4aEnd",
                "t4", "t5", "t6", "t6b", "t6c", "t6d", "t7", "t8"
        };
        for (int i = 0; i < kMilestoneCount; ++i) {
            appendMs(out, kKeys[i], offsetMs(static_cast<Milestone>(i)),
                     i != kMilestoneCount - 1);
        }
        out += "}}";
        return out;
    }
}
