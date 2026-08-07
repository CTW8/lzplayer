# lzplayer-test-expert Agent 设计文档

> 创建日期: 2026-07-22
> 实施跟踪: ../features/lzplayer-test-expert/

## 背景与目标

lzplayer 的播放链路横跨 Java UI → JNI → C++ 引擎（VEPlayer），改动后的回归验证目前靠手工操作真机，成本高且容易漏测。

目标：构建一个专用测试 agent（`lzplayer-test-expert`），在真机/模拟器上自动执行播放器全功能测试，过程中监控日志，发现问题自动结合源码定位根因，最终产出结构化测试报告。

## 能力范围

1. **视觉驱动的操作闭环**：截图 → 分析界面 → 生成点击/滑动命令 → 执行 → 再截图验证，循环推进用例；控件坐标通过 uiautomator dump 定位，不估坐标。
2. **全功能用例集**：文件选择、播放/暂停/恢复/停止、进度条 seek、时间显示、EOS 行为、前后台切换/Surface 重建等边界场景。
3. **日志监控**：每个用例执行前清 logcat 缓冲，执行后 dump 过滤分析（crash/ANR/native 崩溃/错误 TAG）。
4. **自动根因分析**：从日志堆栈/TAG 映射到具体代码层（app Java UI / lzplayer_core JNI / C++ 引擎），Read+Grep 源码给出"错在哪个文件哪段逻辑、为什么"。
5. **测试报告**：落地 `test-reports/YYYY-MM-DD-HHMM.md`，含用例结果（Pass/Fail + 截图证据）与问题列表（现象 → 日志证据 → 代码定位 → 根因 → 修复建议）。

## 技术方案

- 按 Claude Code subagent 规范落在 `.claude/agents/lzplayer-test-expert.md`（frontmatter: name/description/tools/model）。
- adb 操作知识复用 adb-ops skill：agent 通过 Read 引用 `.claude/skills/adb-ops/references/commands.md`，不重复维护两份命令知识。
- 代码定位映射写入 agent prompt：回调链路（NativeLib.EventHandler → IVEPlayerListener）、消息驱动模型（VEPlayerDriver → AMessage → VEPlayer）等项目事实，帮助从日志快速切入代码。
- 截图/日志/ui.xml 等过程产物放 scratchpad，仅报告落项目 `test-reports/`。

## 风险与依赖

- 依赖设备已连接、debug 包已安装、设备上有可播放的测试视频（无则需用户提供或提前 push）。
- SurfaceView 渲染内容无法通过控件树断言，画面正确性只能靠截图人工可见性判断（黑屏/花屏可判，画质细节不可判）。
- uiautomator dump 偶发失败，需要降级为纯截图定位。
