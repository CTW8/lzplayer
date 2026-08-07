#ifndef LZPLAYER_IVECOMPONENT_H
#define LZPLAYER_IVECOMPONENT_H

#include "VEError.h"

namespace VE {

    /// 管线组件的统一命令面(仿 NuPlayer 各组件的一致生命周期)。
    ///
    /// 播放器只按这个接口对全体组件扇出命令、按角色状态机收回执，
    /// 因此新增组件(硬解解码器、字幕轨、网络源)不需要改动 VEPlayer
    /// 的任何编排逻辑——注册进 Role 表即可参与 seek/teardown 的分阶段握手。
    ///
    /// 纪律：所有命令都是异步的(投消息进组件自己的 looper)，完成后经
    /// 构造时传入的 notify 通道回执对应的 *_DONE 事件；同步返回值只表示
    /// "命令是否成功投递"，不代表执行完成。
    class IVEComponent {
    public:
        virtual ~IVEComponent() = default;

        virtual VEResult start() = 0;

        /// 停止消费数据；完成后回 VE_NOTIFY_EVENT_STOP_DONE
        virtual VEResult stop() = 0;

        /// 暂停消费数据；完成后回 VE_NOTIFY_EVENT_PAUSE_DONE
        virtual VEResult pause() = 0;

        /// 定位并清理内部缓冲；完成后回 VE_NOTIFY_EVENT_SEEK_DONE
        virtual VEResult seekTo(double timestampMs) = 0;

        /// 清空内部缓冲(不改变播放位置)；完成后回 VE_NOTIFY_EVENT_FLUSH_DONE
        virtual VEResult flush() = 0;

        /// 在组件自己的线程上释放资源(编解码器/EGL/SLES 都必须如此)；
        /// 完成后回 VE_NOTIFY_EVENT_RELEASE_DONE，播放器收齐才停 looper
        virtual VEResult release() = 0;
    };
}

#endif //LZPLAYER_IVECOMPONENT_H
