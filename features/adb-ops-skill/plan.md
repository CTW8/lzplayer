# adb-ops Skill 实施计划

> 设计文档: ../../docs/adb-ops-skill-design.md
> 创建日期: 2026-07-22

## 方案摘要

在 `.claude/skills/adb-ops/` 下按 Claude Code skill 规范创建 adb 操作技能：SKILL.md 承载触发条件与核心工作流（抓日志、拉文件、截图、启动 app、UI 点击），references/commands.md 承载 adb 命令全量参考。采用渐进式披露，SKILL.md 精简、细节下沉参考手册。

## 实施步骤

1. 创建 `references/commands.md`：adb 命令全量参考（设备管理/日志/文件/截图录屏/应用控制/UI 交互/系统信息），每条命令附用途说明。验收：覆盖设计文档能力范围全部 5 类。
2. 创建 `SKILL.md`：frontmatter（name/description/allowed-tools）+ 高频工作流（含"截图→dump→定位→点击→验证"闭环）+ 关键约定。验收：符合 Claude Code skill 规范，description 覆盖全部触发场景。
3. 验证：确认 skill 出现在可用列表并可被"截图看一下手机"之类的请求触发（需重启会话生效，人工验证）。
