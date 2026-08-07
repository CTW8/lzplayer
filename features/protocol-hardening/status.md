# protocol-hardening 进度

> 最后更新: 2026-07-23
> 总体状态: Done（9/9 步骤，全部提交；真机回归挂起，见遗留事项）
> 全部 commit: b64758f / 824b7a2 / f8d87be / ceb436d / 2a4bc56 / badf9d4 / 1918191 / 8ed3857 / 982efa7

## Done
- [x] 步骤 1: 接口移除与调用点显式化（暂保位掩码保证可编译） (2026-07-23，commit b64758f，assembleDebug 通过零警告)
  - 计划外说明 ①：死代码 VEVideoRender / AudioOpenSLESOutput 四个文件因 include 已删除的 IVEComponent 阻断编译，本步顺势删除（原属另立的卫生债 feature，该项已提前完成）。
  - 计划外说明 ②：VEAudioRender EOS 越权 pause 解码器的删除（原计划步骤 6 的 D2 项，`VEAudioRender.cpp:233`）因接口移除被迫提前到本步完成，步骤 6 实施时该项跳过。

- [x] 步骤 2: 角色状态机 + pipelineGen + 超时新语义（删位掩码全套）——控制面最大步 (2026-07-23，commit 824b7a2，assembleDebug 通过零警告)

- [x] 步骤 3: 操作串行化队列 + 删换源分支与延时重投 (2026-07-23，commit f8d87be，assembleDebug 通过零警告)

- [x] 步骤 4: 包平面改造（10ms 重试 + 拉取触发补货 + 删 needMorePacket 机制 + demux epoch/mReleased/onRead 判空） (2026-07-23，commit ceb436d，assembleDebug 通过零警告)

- [x] 步骤 5: 帧平面推模型·视频链（推 + credit，删视频拉链路） (2026-07-23，commit 2a4bc56，assembleDebug 通过零警告)

- [x] 步骤 6: 帧平面推模型·音频链 + 音频驱动改造（删静音保活、预填、EOS 自停；删 IMediaDecoder 收尾）——全方案最大步 (2026-07-23，commit badf9d4，assembleDebug 通过零警告；D2 项已在步骤 1 提前完成，本步跳过)

- [x] 步骤 7: ERROR/EOS 收敛 + Driver F2 (2026-07-23，commit 1918191，assembleDebug 通过零警告)

- [x] 步骤 8: 异步 prepare + AVIOInterruptCB (2026-07-23，commit 8ed3857，assembleDebug 通过零警告)

- [x] 步骤 9: seek 细节（resetTo 保持暂停态；无 surface 时补发 FIRST_FRAME） (2026-07-23，commit 982efa7，assembleDebug 通过零警告)

## Doing
（无）

## Todo
（无）

## 遗留事项
- **真机回归测试挂起**：按用户指示，全部步骤完成后一次补测。验收用例清单见 plan.md「验收标准（整体）」，重点：起播预缓冲与首帧、连续拖动进度条、暂停态 seek、seek 片尾、播完重播与循环、坏文件 prepare 中 release、反复 create→play→release 线程与内存、纯音频/纯视频文件、underrun 恢复、转屏与 surface 销毁重建、播完静置功耗。可走 lzplayer-test-expert 流程补测。
- **全局卫生债 feature 尚未登记**：待办项包括 Java 调试日志清理、attached_pic 处理、VEPacket/VEFrame 拷贝语义、CMake GLOB 改显式列表、finalizer 兜底等。其中 VEVideoRender / AudioOpenSLESOutput 死文件删除已在本 feature 步骤 1 提前完成（commit b64758f），登记时应标注该项已完成。
