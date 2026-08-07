# lzplayer-test-expert Agent 实施计划

> 设计文档: ../../docs/lzplayer-test-expert-design.md
> 创建日期: 2026-07-22

## 方案摘要

在 `.claude/agents/lzplayer-test-expert.md` 按 Claude Code subagent 规范创建播放器专用测试 agent：视觉驱动（截图→定位→点击→验证）执行全功能用例集，逐用例监控 logcat，异常时结合源码（Java/JNI/C++ 三层映射）定位根因，报告落 `test-reports/`。adb 命令知识复用 adb-ops skill 的参考手册。

## 实施步骤

1. 创建 `.claude/agents/lzplayer-test-expert.md`：frontmatter + 完整测试流程（前置检查/用例集/操作规范/日志分析/代码定位映射/报告模板）。验收：符合 subagent 规范，覆盖设计文档全部 5 项能力。
2. 更新根目录 CLAUDE.md：登记该 agent 的用途与 test-reports/ 目录约定。验收：文档索引与使用说明一致。
3. 验证：agent 出现在可用 agent 列表。验收：Agent 工具可见 lzplayer-test-expert。
