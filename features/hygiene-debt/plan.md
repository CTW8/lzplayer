# hygiene-debt 代码卫生债清理 实施计划

> 设计文档: ../../docs/hygiene-debt-design.md
> 创建日期: 2026-07-24

## 方案摘要
清理 protocol-hardening / demux-buffering review 期间陆续标记、归口本 feature 的低风险卫生债。
全部为清理/加固项，不改播放行为。每步独立 assembleDebug 编译验证、独立 commit，互不阻塞。
步骤5（CMake 显式源列表）为可选、放最后，用户可否决。真机回归按用户指示挂起。

## 实施步骤
1. 调试日志与残留清理：NativeLib.java / VEPlayer.java 的 🚀🔥 Log.e 降级 Log.d 或删除；
   VEDemux::onPrepare 的 printf/fprintf(约3处)改 ALOGE；清注释掉的 dump 代码块等残留。
   验收：assembleDebug 零警告，release 不再刷调试日志。
2. 死成员清理 + 拷贝语义加固：删 VEPlayer::mAPacketQueue；对 VEPacket(VEPacket*)/operator=、
   VEFrame::operator= 逐一 grep 确认无调用点后改 =delete（或改深拷贝）。
   验收：grep 证明无调用点，assembleDebug 零警告。
3. getFileInfo 缓存：VEDemux::getFileInfo 缓存 shared_ptr<VEMediaInfo>，prepare 后直接返回。
   验收：不再每次 new，assembleDebug 零警告。
4. Java 生命周期兜底：NativeLib 加 finalize() 兜底调 release()（minSdk=24 不能用 Cleaner），
   主契约仍是显式 release()。验收：assembleDebug 零警告。
5. （可选，最后，用户可否决）CMake GLOB → 显式源列表：CMakeLists 改显式 .cpp 列表；
   改后 clean build 核对 .so 符号无缺失。验收：clean build 通过，符号无缺失。
