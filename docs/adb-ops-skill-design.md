# adb-ops Skill 设计文档

> 创建日期: 2026-07-22
> 实施跟踪: ../features/adb-ops-skill/

## 背景与目标

LZPlayer 是 Android 播放器项目，日常开发/调试高频依赖 adb：抓 logcat 定位 native 崩溃、拉取应用产生的文件、截图确认渲染效果、启动 app 并模拟点击验证播放链路。目前这些操作靠临时手敲命令，知识分散。

目标：提供一个 Claude Code skill（`adb-ops`），封装 adb 全量操作知识，使 Claude 在调试/验证场景下能规范、可靠地执行设备操作。

## 能力范围

1. **日志抓取**：logcat（按 TAG/PID/级别过滤、dump/持续两种模式、保存到文件）、bugreport、native crash（tombstone）。
2. **文件读取**：
   - 手机公开文件（/sdcard 等）：`adb pull/push`
   - 应用内部文件（/data/data/<pkg>）：debuggable 包走 `run-as`，非 debuggable 说明限制
3. **截图/录屏**：`screencap` 截图后用 Read 工具直接查看；`screenrecord` 录屏并拉取。
4. **应用控制**：启动/停止/清数据/查安装信息/查当前前台 Activity。
5. **UI 交互**：`input tap/swipe/keyevent/text`；配合 `uiautomator dump` 解析控件 bounds 计算点击坐标，形成"截图→定位→点击→验证"闭环。

## 技术方案

按 Claude Code skill 规范落在 `.claude/skills/adb-ops/`：

```
.claude/skills/adb-ops/
  SKILL.md               # 触发条件 + 核心工作流（渐进式披露，保持精简）
  references/
    commands.md          # adb 命令全量参考手册（按类别组织）
```

- SKILL.md frontmatter 含 `name`/`description`/`allowed-tools`；description 覆盖触发词（抓日志、截图、启动 app、点击、拉文件等）。
- 全量命令细节放 references/commands.md，SKILL.md 只保留高频工作流与关键约定，避免撑爆上下文。
- 关键约定：多设备时用 `-s`；截图/文件一律先落 scratchpad 再 Read；点击前必须用 uiautomator dump 定位，不猜坐标。

## 风险与依赖

- 依赖本机 adb 已安装、设备已连接并授权。
- `run-as` 仅对 debuggable 包有效（本项目 debug 包满足）。
- `input text` 不支持中文，需注明替代方案。
