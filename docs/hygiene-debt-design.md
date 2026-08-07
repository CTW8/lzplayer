# hygiene-debt 代码卫生债清理 设计文档

> 创建日期: 2026-07-24

## 背景与目标

在 `protocol-hardening` 与 `demux-buffering` 两个 feature 的多轮 review 过程中，陆续发现一批
低风险的"卫生问题"（调试残留、死代码、危险拷贝语义、缺生命周期兜底等）。当时为避免污染功能
feature 的提交历史与回归范围，刻意不将其混入，统一归口到本 feature 集中清理。

**目标**：清理/加固既有代码，消除易踩坑点，降低后续维护成本。

**明确边界**：本 feature 全部为清理/加固项，**不改变任何播放行为**。凡涉及行为语义变化的
优化项都不在此范围内（应另立 feature）。

## 技术方案

按风险与关联度拆为 5 步，每步独立 `assembleDebug` 编译验证、独立 commit，互不阻塞。

### 步骤1: 调试日志与残留清理
- `NativeLib.java` / `VEPlayer.java` 中成片的 🚀🔥 `Log.e`（如 "About to call nativeStart" 等）
  降级为 `Log.d` 或删除——release 版不应刷这些日志。
- `VEDemux::onPrepare` 中的 `printf` / `fprintf(stderr, ...)`（约 3 处）改为 `ALOGE`。
- 其它明显调试残留（注释掉的 dump 文件代码块等）酌情清理。

### 步骤2: 死成员清理 + 拷贝语义加固
- 删 `VEPlayer::mAPacketQueue`（声明后从未使用）。
- 危险拷贝/赋值语义加固：
  - `VEPacket(VEPacket*)` 拷贝构造浅拷贝裸 `AVPacket*` → 两对象析构同一包 double-free；
  - `VEPacket::operator=` 同源问题；
  - `VEFrame::operator=` 先覆写 `mFrame` 泄漏原帧再 ref 自身。
- 处置策略：逐一 grep 确认全项目无调用点后，将危险的拷贝/赋值改为 `=delete`，把误用从
  运行时 double-free 提前为编译错误；若某处确有需要则改为深拷贝语义。

### 步骤3: getFileInfo 缓存
- `VEDemux::getFileInfo` 每次调用都 `new` 一个 `VEMediaInfo` 逐字段拷贝返回。prepare 后媒体
  信息不变，缓存一份 `shared_ptr` 直接返回，避免重复分配。

### 步骤4: Java 生命周期兜底
- `NativeLib` 无 finalizer/Cleaner 兜底，上层忘调 `release()` 则 native `VEPlayerDriver` +
  looper 线程 + JNI GlobalRef + HandlerThread 全泄漏。
- 约束：minSdk=24，`java.lang.ref.Cleaner` 需 API33 不可用。
- 方案：采用 `finalize()` 兜底调 `release()`（对齐 AOSP MediaPlayer 历史做法）。主契约仍是
  显式 `release()`，`finalize` 仅 best-effort。

### 步骤5（可选，最后，用户可否决）: CMake GLOB → 显式源列表
- `CMakeLists.txt` 用 `file(GLOB_RECURSE)` 收集源文件，删文件时留陈旧缓存（本会话已多次踩坑）。
- 改为显式 `.cpp` 列表，增量构建确定性更好。
- 代价：新增源文件需手动登记。风险：漏列源文件会静默丢符号，改后需 clean build 核对 `.so`
  符号无缺失。
- 标注为可选、放最后，用户可跳过。

## 涉及模块
- `lzplayer_core`：`VEPlayer`、`VEDemux`、`VEPacket`、`VEFrame`、JNI `NativeLib`、`CMakeLists.txt`
- `app`：`VEPlayer.java`（Java 层）

## 风险与依赖
- 步骤2 删拷贝/赋值前**必须** grep 全项目确认无调用点。
- 步骤5 有静默丢符号风险，需 clean build 核对 `.so` 符号无缺失。
- 真机回归：按用户指示挂起，本 feature 均为清理项，解除限制后与其它 feature 一起统一回归。
