# demux-nuplayer-refactor 实施计划

> 设计文档: ../../docs/demux-nuplayer-refactor-design.md
> 创建日期: 2026-07-30
> 阶段范围: 阶段一（接口边界解耦，不改驱动模型）

## 方案摘要

把 VEPlayer 对 VEDemux 具体类型的持有解耦为对 VESource 抽象接口的持有。新建 `interface/VESource.h`（继承 AHandler + IMediaSource，命令面纯虚 + `postNotify` 统一回执）；VEDemux 改单继承 VESource，`prepare` 改名 `prepareAsync`，`postMessage` 上提为 `VESource::postNotify`；VEPlayer 成员 `mDemux`→`mSource`(类型 VESource)、`mDemuxLooper`→`mSourceLooper`，新增 `createSource` 工厂；解码器零改动，VEPlayer 传 `static_pointer_cast<IMediaSource>(mSource)`。驱动模型内核（读循环自驱、消费侧拉取唤醒、命令面消息回执、mReleased/interrupt）行级保持不变。

## 实施步骤

1. **新建 VESource.h 抽象基类**
   - 目标：在 `interface/VESource.h` 新增抽象基类，继承 `AHandler` + `IMediaSource`；声明命令面纯虚 `prepareAsync/start/stop/pause/seekTo/flush/release/abort`；提供 protected `mNotify` + `postNotify(event,arg1,arg2,arg3,params)`（复刻现 VEDemux::postMessage 实现：dup mNotify → setInt32 type=E_COMPONENT_TYPE_DEMUX/event/arg1/arg2 → setInt64 arg3 → setPointer params → post；plGen 不在此设，由 notify 模板携带）。
   - 验收：文件存在且可被 include；include 路径（AHandler/AMessage/IMediaSource/VEDef/VEError）可达；本步不强制编译通过（VEDemux 尚未改继承）。

2. **VEDemux 改继承 VESource、prepare 改名 prepareAsync、postMessage 上提 postNotify**
   - 目标：`class VEDemux : public VESource`；构造 `VEDemux(notify)` 调 `VESource(notify)`，删 `mNotifyEvent`，内部 `mNotifyEvent->` 全改 `mNotify->`；`prepare` 改名 `prepareAsync`（语义不变，仍投 kWhatPrepare，回执 VE_NOTIFY_EVENT_PREPARE_DONE）；命令方法 + read/getFileInfo/onMessageReceived 全部加 `override`；删 `postMessage` 声明与实现，所有 `postMessage(...)` 调用点改 `postNotify(...)`；onMessageReceived/读循环/shouldParkRead/scheduleContinueReadIfNeeded/kWhatContinueRead/interruptCallback/mReleased/枚举/成员变量原样保留。
   - 验收：`./gradlew assembleDebug` 编译通过（此步完成后理论上可编译，因 VEDemux 仍被 VEPlayer 以具体类型持有，prepareAsync 调用点待下一步同步改名——若编译报 VEPlayer 调 prepare() 未定义，属预期，合入步骤3后即消）。

3. **VEPlayer mDemux→mSource + createSource 工厂 + 全量调用点改名**
   - 目标：`VEPlayer.h` 成员 `std::shared_ptr<VEDemux> mDemux` → `std::shared_ptr<VESource> mSource`，`mDemuxLooper` → `mSourceLooper`；新增 `std::shared_ptr<VESource> createSource(const std::string &path)` 声明。`VEPlayer.cpp` 新增 `createSource` 实现（当前只 `return std::make_shared<VEDemux>(mRenderNotifyMsg);`，预留 scheme 分支注释）；`setupDataSource` 里 looper 创建/setName/registerHandler 改 mSourceLooper + mSource + createSource；`doPrepare`/`continuePrepare`/`start`/`stop`/`seek`/`pause`/`teardown`/`abort` 路径里所有 `mDemux->` 改 `mSource->`、`mDemux` 判空改 `mSource`、`mDemuxLooper` 改 `mSourceLooper`、`.reset()` 改对应名；`mDemux->prepare(mPath)` 改 `mSource->prepareAsync(mPath)`；`mDemux->getFileInfo()` 改 `mSource->getFileInfo()`；传解码器的 `mDemux` 改 `std::static_pointer_cast<IMediaSource>(mSource)`。
   - 验收：`./gradlew assembleDebug` 编译通过，零新增警告。

4. **编译通过整体验证**
   - 目标：全量 `./gradlew assembleDebug` 干净构建（clean 后重建），确认 VESource 多继承无菱形/符号问题，postNotify 字段与 onComponentEvent 解析对齐，解码器 prepare 经 static_pointer_cast 编译无误。
   - 验收：clean build 通过，无新增警告。

5. **真机回归（prepare/start/pause/seek/EOS/换源 reset）**
   - 目标：用 lzplayer-test-expert 跑全功能回归，覆盖起播、暂停/恢复、seek（含 flush+seekTo+SEEK_DONE）、EOS、换源 reset（teardown 后重新 prepare 新文件）；确认驱动模型内核行为无回归（读循环、节流、续读去重、interrupt 解阻塞）。
   - 验收：回归报告落 `test-reports/`，上述用例全绿。
