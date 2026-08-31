# hygiene-debt 代码卫生债清理 进度

> 最后更新: 2026-09-01
> 总体状态: **Done（必做 4 步 + 新增的 1 条卫生债全部完成）**

## Done
- [x] 步骤 1: 调试日志与残留清理（NativeLib.java / VEPlayer.java 的 🚀🔥 Log.e 降级或删除；VEDemux::onPrepare 的 printf/fprintf 改 ALOGE；清注释掉的 dump 残留）(2026-07-24, commit 9740456)
- [x] 步骤 2: 死成员清理（删 VEPlayer::mAPacketQueue）+ 拷贝语义加固（VEPacket/VEFrame 危险拷贝赋值改 =delete，编译器确认全项目无调用点）(2026-07-24, commit 11f9889)
- [x] 步骤 3: VEDemux::getFileInfo 缓存 shared_ptr<VEMediaInfo>，prepare 后直接返回 (2026-07-24, commit 6115acd)
- [x] 步骤 4: NativeLib 加 finalize() 生命周期兜底调 release()（minSdk=24 用 finalize 而非 Cleaner，主契约仍是显式 release()）(2026-07-24, commit 382a45e)
- [x] **`test-reports/raw/` 不再被 git 跟踪** (2026-09-01, commit `8f4fa97`)
      —— 2026-09-01 由 net-playback-harness 步骤10 的矩阵回归暴露：该目录每跑一轮场景矩阵
      产生**约 17 万行 diff**，**任何正常的代码提交都会被它淹没**，review 与归因都做不了；
      与素材目录已 gitignore 的做法不一致（步骤1 曾实测"3.8G 零泄漏"）。
      经用户拍板后按"raw 是中间产物、汇总与报告仍入库"的口径处理。

## Doing

（空）

## Todo

（空）

## 决策记录（不计入三态）
- 步骤 5:（可选项）CMakeLists GLOB → 显式源列表 —— 用户否决，不实施。理由：GLOB 便利性优先，陈旧缓存罕见且 clean build 即可解决。(2026-07-24)

## 遗留
- 真机回归：按用户指示挂起，与其它 feature 一起在解除限制后统一回归。
