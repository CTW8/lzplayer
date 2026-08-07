# demux-nuplayer-refactor 进度

> 最后更新: 2026-07-30
> 总体状态: Doing

## Done
- [x] 步骤 1: 新建 interface/VESource.h 抽象基类 (2026-07-30) — 文件 lzplayer_core/src/main/cpp/interface/VESource.h 已创建；class VESource : public AHandler, public IMediaSource；命令面纯虚 prepareAsync/start/stop/pause/seekTo/flush/release/abort；数据面 read/getFileInfo 继承自 IMediaSource；protected postNotify 逐字段复刻原 VEDemux::postMessage，内部不设 plGen（由 VEPlayer 构造 notify 模板写定、dup 携带）；纯新增头文件，未改现有文件。
- [x] 步骤 2: VEDemux 改继承 VESource、prepare 改名 prepareAsync、postMessage 上提 postNotify (2026-07-30) — VEDemux.h 改 `class VEDemux : public VESource`（去掉 AHandler/IMediaSource 直接继承，加 VESource.h include）；prepare→prepareAsync 加 override；start/stop/pause/seekTo/flush/release/abort/read/getFileInfo/onMessageReceived 全部加 override；删除 postMessage 声明与 mNotifyEvent 成员。VEDemux.cpp 构造初始化列表改 `: VESource(notify)`，删 mNotifyEvent 赋值；prepare→prepareAsync；6 处 postMessage 调用改 postNotify；删除 postMessage 实现（逻辑已在 VESource::postNotify）。驱动内核（onMessageReceived/读循环/shouldParkRead/kWhatContinueRead/interruptCallback/mReleased）原样保留。
- [x] 步骤 3: VEPlayer mDemux→mSource + createSource 工厂 + 全量调用点改名 (2026-07-30) — VEPlayer.h include 由 VEDemux.h 改 VESource.h；mDemux→`std::shared_ptr<VESource> mSource`；mDemuxLooper→mSourceLooper；新增 createSource 声明。VEPlayer.cpp 加 `#include "VEDemux.h"`（工厂需具体类型）；mDemuxLooper→mSourceLooper、mDemux→mSource 全量替换；`mDemux->prepare(mPath)`→`mSource->prepareAsync(mPath)`；setupDataSource 改用 `createSource(path)` 构造源；新增 createSource 实现（当前返回 `std::make_shared<VEDemux>(mRenderNotifyMsg)`，预留 scheme 分支注释）。解码器 prepare(mSource,...) 经 IMediaSource 接口不变。事件分发 E_COMPONENT_TYPE_DEMUX 分支与 mSourceState 角色映射未动。
- [x] 步骤 4: 编译通过整体验证（clean build） (2026-07-30) — `./gradlew :lzplayer_core:externalNativeBuildDebug` BUILD SUCCESSFUL。期间 LSP 报的 include 错误均为 LSP 未读 CMakeLists 的 include path 误报，真实 CMake 构建通过证伪。

## Doing
- [ ] 步骤 5: 真机回归（prepare/start/pause/seek/EOS/换源 reset） — 等待主对话触发 lzplayer-test-expert agent 跑全功能回归

## Todo
