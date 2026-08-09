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
            /// 硬解 configure 内部三段的边界。拆开是因为"预热什么"完全取决于
            /// 哪一段占大头：createDecoderByType 是组件加载 + binder 到 codec2
            /// 服务(可提前预建实例)，AMediaCodec_configure 依赖轨道参数与 csd
            /// (只能靠与 find_stream_info 并行来隐藏)。软解不填这两个点。
            T4A_CODEC_CREATED,       ///< createDecoderByType 返回
            T4A_CODEC_CONFIGURED,    ///< AMediaCodec_configure 返回
            T4A_CONFIGURE_END,       ///< 视频解码器 configure 完成(含 start)
            T4_CHAIN_READY,          ///< 建链完成(PREPARED)
            T5_START,                ///< start 生效
            T6_FIRST_FRAME_DECODED,  ///< 第一帧解出
            /// T6→T7 的内部拆分。这段实测 51~98ms 且方差极大，先后被误判成
            /// "纹理分配"和"等同步时钟"两次——两次都是拿名字推构成。拆开看：
            /// 跨线程投递 / 进同步判定 / 同步要求等多久 / 进渲染 / 渲染本身。
            T6B_FRAME_QUEUED,        ///< 显示端 looper 收到首帧
            T6C_SYNC_ENTER,          ///< 首次进入 onAVSync
            T6D_RENDER_ENTER,        ///< 首次进入 onRender
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

        /// 只有 gate 已被标记后才允许标记 m。
        ///
        /// 用于落在**重复执行**路径上的里程碑：onAVSync / onRender 在首帧到达
        /// 之前就随同步循环跑起来了，不门控的话 mark() 的"首次生效"会记到一个
        /// 与首帧毫无关系的时刻(实测 T6C 就是这么记错的)。
        /// 判据是：这条路径在目标事件之前会不会先跑。
        void markIfArmed(Milestone gate, Milestone m);

        bool marked(Milestone m) const;

        /// 硬解与软解的 T7 物理含义不同(见 toJson 注释)，必须随数据一起带出去
        void setDecodePath(bool hardware);

        /// 同步逻辑为首帧算出的等待时长。这是判断"是不是在等时钟"的唯一
        /// 直接证据——里程碑只能给出经过了多久，给不出"它被要求等多久"
        void setFirstFrameWaitUs(int64_t waitUs);

        /// 序列化为 JSON。未采集到的量输出 -1，由上层显示为 "--"，
        /// 不要用 0 兜底——0 会被当成"极快"，比缺失更危险。
        std::string toJson() const;

    private:
        /// 单位纳秒，-1 表示未采集
        int64_t mAt[kMilestoneCount];
        bool mHardware;
        bool mHasDecodePath;
        int64_t mFirstFrameWaitUs;
        bool mHasFirstFrameWait;
        mutable std::mutex mMutex;

        /// 两个里程碑之间的毫秒差。
        /// 任一端缺失返回 **NaN**(序列化成 JSON null)；采到了就返回真实值，
        /// **允许为负**——负数只可能是采集点顺序颠倒，是必须显眼的 bug 信号。
        /// 早先两种情况都返回 -1，把"没采到"和"顺序错了"压成同一个值，
        /// T6C 记错了因此一直没被发现。
        double spanMs(Milestone from, Milestone to) const;
        /// 相对 T0_REQUEST 的毫秒偏移
        double offsetMs(Milestone m) const;
    };
}

#endif //LZPLAYER_VESTARTUPTRACE_H
