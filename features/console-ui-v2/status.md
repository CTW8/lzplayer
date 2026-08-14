# console-ui-v2 进度

> 最后更新: 2026-08-14
> 总体状态: Doing

## Done

（无）

## Doing

- [ ] 步骤0: 前置 —— 提交工作区两处已验证但未提交的改动 — **本次开工项**
  - 内容属 perf-metrics 步骤5 剩余项 1、2：① 性能面板全屏拖动（`isFitToContents=false` +
    `expandedOffset=0`）；② 清除每 tick 写 perf 文件的临时脚手架。
  - 该脚手架的 `writeText` **先截断再写**，调试 stop→play 时已读到空文件并**误导过一次**，必须清掉。
  - 涉及文件：`app/src/main/java/com/example/lzplayer/console/ConsoleActivity.kt`、
    `app/src/main/java/com/example/lzplayer/console/DiagnosticsSheet.kt`（当前均为 modified）。
  - **commit 需用户明确同意后执行**（CLAUDE.md Git 操作规范）。
  - 完成后同步把 perf-metrics 步骤5 剩余项 1、2 标 Done。

## Todo

- [ ] 步骤1: 布局重构 —— `activity_console.xml` 三层控件区 + 进度行内嵌 26px 播放圆钮，
      面板入口四等宽同样式（性能/轨道/字幕/跳转），次要控件纯文字，停止靠右危险色字色。
- [ ] 步骤2: 常驻仪表带（两行八格，颜色+数值双编码，三态显示，丢帧只取 `dropLate`，
      chip 补分辨率与帧率），复用 PlayerStats / 启播 trace / 稳态直方图 / 资源采样。
- [ ] 步骤3: 源栏折叠 / 展开（一行摘要 + 「换源」，点击展开回完整输入）。
- [ ] 步骤4: 仪表格子点击跳转到指定分页（`DiagnosticsSheet` 增"打开时定位到某页"入口参数；
      本期只做到落在正确分页，自动滚动并高亮列为后续增强）。
- [ ] 步骤5: 横屏沉浸态调整（控件 3s 自动隐藏，**chip 与左下 HUD 常驻不隐藏**）。
- [ ] 步骤6: 真机截图核对（竖屏 / 横屏 3s 前后 / 灰度可读性 / 八格跳转映射逐项），
      标准素材 1080p，报告落 `test-reports/`；**建议与 test-console-ui 步骤8 合并成一轮**。

## 跨 feature 影响（登记时即记录）

- **test-console-ui 步骤8**：其 A 屏（主控台）与 D 屏（横屏沉浸）的核对标准将因本次重构作废，
  需按新布局重写核对项。已在 test-console-ui/status.md 标注。
- **perf-metrics 步骤5**：本 feature 步骤0 完成即可让其剩余项 1、2 转 Done，
  帮助 perf-metrics 的 Doing 段从 3 个降到 2 个。
  本 feature 对 `DiagnosticsSheet.kt` 只做最小新增（入口参数），不动分页内部实现。
