# CLAUDE.md

LZPlayer — 基于 FFmpeg 的 Android 音视频播放器，多模块架构（Java / Kotlin / C++）。

## 文档索引

| 文档 | 内容 | 何时阅读 |
|------|------|----------|
| [README.md](README.md) | 项目介绍、功能特性、模块结构、使用说明 | 初次了解项目、构建运行 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 完整架构文档：模块依赖、C++ 引擎架构、数据流 | 修改核心逻辑、跨模块开发前 |
| [AGENTS.md](AGENTS.md) | 模块组织、核心流程（选择/播放/回调链路）、lzplayer_core 内部架构 | 日常开发的首要参考 |
| [TODO.md](TODO.md) | 历史待办事项 | 参考用；新需求一律走 feature-tracker |
| `docs/` | 各需求的方案/设计文档（由 feature-tracker 维护） | 实施某个 feature 前 |
| `features/` | 各需求的实施计划与进度（由 feature-tracker 维护） | 每次开发前后 |
| `test-reports/` | 真机测试报告（由 lzplayer-test-expert 生成） | 回归测试后、修 bug 前 |

## 开发规范

### 构建与环境
- 构建：`./gradlew assembleDebug`；Gradle 8.12，NDK 25.1.8937393，CMake 3.18.1/3.22.1。
- 仅支持 arm64-v8a；minSdk 24，targetSdk 33。
- Native 改动后需完整重新构建对应模块（CMake 侧改动 Gradle 不一定感知）。

### 代码规范
- 模块边界：`app`（UI）→ `lzplayer_core`（VEPlayer 引擎 JNI/C++）、`MediaSelector`（Kotlin 文件选择）、`MediaPipeline`、`VERecorder`。禁止跨模块直接引用内部实现类。
- Java/Kotlin：遵循 AOSP 风格；新 UI 相关代码优先 Kotlin。
- C++ 层：消息驱动模型（`AHandler`/`AMessage`/`ALooper`），新增播放器能力应走 `VEPlayerDriver` → `AMessage` 事件分发，不要在 JNI 层直接做业务逻辑。
- 回调链路：Native → `NativeLib.EventHandler` → `IVEPlayerListener`，新增事件要保持这条链路，不要另起通道。
- 注释与提交信息可用中文；commit message 简明描述改动目的。

### 测试
- 单测放各模块 `src/test/java`，仪器测试放 `src/androidTest/java`。
- 涉及播放核心（seek/状态机/解码）的改动，需在真机上验证播放、暂停、seek、EOS 基本链路——用 **lzplayer-test-expert** agent 跑全功能回归（说"测试播放器"即可触发），报告落 `test-reports/`。
- 单次设备操作（截图/抓日志/点击/拉文件）用 **adb-ops** skill，不必启动完整测试流程。

### Git 操作规范
- **禁止未经明确同意自动提交代码**：任何 commit（包括创建新 commit、amend、rebase 等）都必须在获得用户明确指示后才能执行。
- **禁止未经明确同意切换/创建分支**：任何分支操作（切换分支、创建新分支、删除分支等）都必须在获得用户明确指示后才能执行。
- **git push 前必须确认**：任何 push 操作（包括 push 新分支、push 到 main、force push 等）都必须向用户说明具体操作并获得明确同意。
- **定期提醒进度**：定期向用户汇报工作进度、当前工作目录状态和待处理事项，而不是自行决策。

## feature-tracker 使用规范

`.claude/agents/feature-tracker.md` 定义了需求跟踪 agent，负责维护 `docs/` 与 `features/` 两个目录。**所有新需求必须经它登记，禁止手工随意建文档。**

### 目录约定
```
docs/<feature-name>-design.md    # 方案设计文档（是什么、为什么）
features/status.md               # 所有需求的总进度表
features/<feature-name>/plan.md  # 实施计划（怎么做，有序步骤）
features/<feature-name>/status.md# 该需求进度，按 Done/Doing/Todo 三态分组
```

### 工作流程
1. **新需求**：用户提出方案 → 调用 feature-tracker → 方案落 `docs/`、计划与进度落 `features/<name>/`、总表登记。
2. **开工**：用户说"开工"/"开始干活"/"继续开发" → **必须先调用 feature-tracker**，由它读取进度、确定（或领取）当前 Doing 步骤并输出工作项，然后主对话按该工作项开始实施。禁止跳过 feature-tracker 直接凭记忆开工。
3. **开发中**：开始某步骤时将其移入 Doing；完成并验证后移入 Done（附日期）。子目录 status.md 与总表必须同步更新。
4. **进展检查**：问 feature-tracker "当前进展如何"，它会基于 status 文件汇报完成度、进行中工作与阻塞项。

### 三态定义
- **Todo**：未开始。
- **Doing**：进行中（同一 feature 同时不超过 1~2 个步骤）。
- **Done**：已完成并通过验证，附完成日期。
