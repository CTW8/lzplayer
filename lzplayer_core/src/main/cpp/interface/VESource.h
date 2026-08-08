#ifndef LZPLAYER_VESOURCE_H
#define LZPLAYER_VESOURCE_H

#include <memory>
#include <string>
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "IMediaSource.h"
#include "IVEComponent.h"
#include "VEDef.h"     // EComponentType
#include "VEError.h"   // VEResult
#include "utils/VEStartupTrace.h"

namespace VE {

    /// 统一媒体源接口（参考 NuPlayer::Source）：
    /// 既是 AHandler（跑在自身 looper 上、收命令消息异步执行），
    /// 又是 IMediaSource（向下游解码器供数）。
    ///
    /// 命令面(prepareAsync/start/stop/...)与数据面(read/getFileInfo)的
    /// 执行纪律由具体实现维持——本抽象只划边界，不规定驱动模型。
    /// 当前实现 VEDemux(LZPlayer 既有内核)：读循环自驱 + 消费侧拉取唤醒
    /// (kWhatContinueRead)、shouldParkRead 节流、interrupt 解阻塞、
    /// mReleased 终态防复活——这些都在子类，本类不涉及。
    ///
    /// 命令异步执行，完成后经构造时传入的 notify 通道回执，VEPlayer 在
    /// onComponentEvent 统一分发(plGen 代际校验在 VEPlayer 侧)。
    class VESource : public AHandler, public IMediaSource, public IVEComponent {
    public:
        explicit VESource(std::shared_ptr<AMessage> &notify) : mNotify(notify) {}

        ~VESource() override = default;

        // —— 命令面：投递到自身 looper 异步执行，回执经 postNotify ——
        /// 异步 prepare：完成后回 VE_NOTIFY_EVENT_PREPARE_DONE(arg1=结果)
        virtual VEResult prepareAsync(const std::string &path) = 0;

        // start/stop/pause/seekTo/flush/release 继承自 IVEComponent(纯虚)

        /// 切换活跃轨道(按对外轨道号)。异步执行，完成后回
        /// VE_NOTIFY_EVENT_SELECT_TRACK_DONE(arg1=结果)。
        /// 默认实现表示"不支持多轨"，仅有单轨的源无需覆写。
        virtual VEResult selectTrack(int trackIndex) {
            (void) trackIndex;
            return VE_INVALID_OPERATION;
        }

        /// 中断阻塞中的 FFmpeg IO(open/read)。可从任意线程调用，同步返回。
        virtual void abort() = 0;

        /// 注入启播里程碑记录器(T1/T2/T3 在源内部打点)。
        /// 故意**不是纯虚**：只有关心启播耗时的源才需要覆写，
        /// 否则每加一个新源都被迫写一遍空实现。
        virtual void setStartupTrace(const std::shared_ptr<VEStartupTrace> &trace) {
            (void) trace;
        }

        // —— 数据面：read / getFileInfo 已继承自 IMediaSource(纯虚) ——

    protected:
        /// 统一回执：逐字段对齐原 VEDemux::postMessage。
        /// plGen 不在此设置——由 VEPlayer 构造 notify 模板时写定，
        /// mNotify->dup() 会自动携带；此处重设会破坏代际校验。
        VEResult postNotify(int32_t event, int32_t arg1, int32_t arg2,
                            int64_t arg3, void *params) {
            std::shared_ptr<AMessage> msg = mNotify->dup();
            msg->setInt32("type", EComponentType::E_COMPONENT_TYPE_DEMUX);
            msg->setInt32("event", event);
            msg->setInt32("arg1", arg1);
            msg->setInt32("arg2", arg2);
            msg->setInt64("arg3", arg3);
            msg->setPointer("params", params);
            msg->post();
            return VE_OK;
        }

        std::shared_ptr<AMessage> mNotify;
    };

} // namespace VE

#endif // LZPLAYER_VESOURCE_H
