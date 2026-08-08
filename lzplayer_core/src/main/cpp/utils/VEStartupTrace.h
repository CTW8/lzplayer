#ifndef LZPLAYER_VESTARTUPTRACE_H
#define LZPLAYER_VESTARTUPTRACE_H

#include <cstdint>
#include <mutex>
#include <string>

namespace VE {

    /// 启播链路的里程碑记录器。
    ///
    /// 起播是一条串行链路，只给一个总耗时说明不了任何问题——必须拆开才知道
    /// 该优化谁。本类只负责"记下每个节点什么时候到"，派生量与序列化都在
    /// toJson() 里一次算完。
    ///
    /// **时间基准一律 steady_clock**：wall clock 会被系统时间调整或 NTP 校准
    /// 打乱，在这种纯粹依赖差值的场景里会产出负耗时。utils/TimeUtils 的
    /// getCurrentTimeMs() 是 wall clock 风格，**不要在这里用**。
    ///
    /// 线程安全：mark() 会被 player / demux / vdec / audio_render 四个 looper
    /// 各自调用，toJson() 又从 JNI 线程调用，因此全程加锁。总调用次数只有
    /// 十几次(每条链路各一次)，锁开销可忽略——这是冷路径，不是每帧热路径。
    ///
    /// 打点本身只做"取一次时间 + 存一个整数"，**不做任何字符串格式化**；
    /// 格式化只在 toJson() 被调用时发生，避免测量行为影响被测对象。
    class VEStartupTrace {
    public:
        enum Milestone {
            T0_REQUEST = 0,          ///< 收到打开请求(onPrepare 入口)
            T0_DISPATCH,             ///< 真正开始执行(排队等待之后)
            T1_CONTAINER_OPEN,       ///< avformat_open_input 返回
            T2_STREAM_INFO,          ///< avformat_find_stream_info 返回
            T3_TRACKS_READY,         ///< buildTrackList 返回
            T4A_CONFIGURE_BEGIN,     ///< 视频解码器 configure 开始
            T4A_CONFIGURE_END,       ///< 视频解码器 configure 完成
            T4_CHAIN_READY,          ///< 建链完成(PREPARED)
            T5_START,                ///< start 生效
            T6_FIRST_FRAME_DECODED,  ///< 第一帧解出
            T7_FIRST_FRAME_PRESENTED,///< 第一帧提交上屏
            T8_FIRST_AUDIO,          ///< 第一个音频帧进设备(时钟首次起锚)
            kMilestoneCount
        };

        VEStartupTrace();

        /// 换源/二次 prepare 前必须调用，否则会残留上一次的值
        void reset();

        /// 记录里程碑。**首次写入生效**，重复调用忽略——
        /// 解码循环里每帧都会走到 T6/T7 那两行，靠这个语义保证只记第一次，
        /// 调用方不必自己维护"是不是首帧"的标志位。
        void mark(Milestone m);

        bool marked(Milestone m) const;

        /// 硬解与软解的 T7 物理含义不同(见 toJson 注释)，必须随数据一起带出去
        void setDecodePath(bool hardware);

        /// 序列化为 JSON。未采集到的量输出 -1，由上层显示为 "--"，
        /// 不要用 0 兜底——0 会被当成"极快"，比缺失更危险。
        std::string toJson() const;

    private:
        /// 单位纳秒，-1 表示未采集
        int64_t mAt[kMilestoneCount];
        bool mHardware;
        bool mHasDecodePath;
        mutable std::mutex mMutex;

        /// 两个里程碑之间的毫秒差；任一端缺失返回 -1
        double spanMs(Milestone from, Milestone to) const;
        /// 相对 T0_REQUEST 的毫秒偏移
        double offsetMs(Milestone m) const;
    };
}

#endif //LZPLAYER_VESTARTUPTRACE_H
