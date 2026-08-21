# 网络播放测试服务 进度

> 最后更新: 2026-08-21
> 总体状态: Doing

## Done

（暂无）

## Doing

- [ ] 步骤1: 素材普查落盘 + manifest（scan-assets.sh，ffprobe 全指纹，
      `is_vfr` 标记 + 缓存排空时间估算，挑选集复制到 serving 目录，
      serving 与 manifest 入 .gitignore）—— **本次开工项，尚未开始**

## Todo

- [ ] 步骤2: media-server.py（Range + 三种注入 + 控制接口 + 字节级请求日志，只绑 127.0.0.1:8188）
- [ ] 步骤3: 接线 smoke（adb reverse + 播一个 http URL）—— **单列一步，失败即是产出，不阻塞后续设计**
- [ ] 步骤4: harness 扩展（run-benchmark.sh 加 --url / 场景编排 / server.log 并入 / 时钟偏移 / 缓存排空时间校验）
- [ ] 步骤5: VESTAT 加 RSS/fd 列（native 小改，longrun 前置；现内存维度为零）
- [ ] 步骤6: 场景矩阵逐个跑（9 个场景，判据双源；**throttle-below 是核心交付**）
- [ ] 步骤7: 探针素材 + 像素断言（含"故意造错确认断言会失败"这一步）
- [ ] 步骤8: 长稳（test2.mp4 53min × 限速 1.2× × 循环，RSS/fd 斜率）
- [ ] 步骤9: 基线归档 test-reports/ + 确认 decoder-starve-wake-dedup 步骤4 解锁

## 备注

- **设计文档 §1.1 更正了一处需求前提**：读代码后确认 `VENetworkSource` 已经用
  自定义 `AVIOContext` + 自写 `VEHttpDataSource`（socket/SSL/Range/重定向）+
  `VEBufferedDataSource`（预取线程 + 32MB 环形缓存 + 水位），
  `avformat_open_input` 路径传 `nullptr` 且置 `AVFMT_FLAG_CUSTOM_IO` ——
  **FFmpeg 的 http 协议根本没被用到**。记忆 [[source-network-io-not-ffmpeg]]
  描述的"未来架构"在这条路径上已经落地。
  本 feature 的定位因此从"未来 feature 的前置"上移为
  "**约 1000 行本项目手写网络代码的首次执行验证**"。
- 已核实的前置事实（降低步骤3 的未知度，但不构成验证）：
  `VESourceRegistry.cpp:24-25` 已注册 http/https scheme；
  `ConsoleActivity.kt:370` 的 `--es source` 原样透传、不校验本地文件存在；
  `AndroidManifest.xml` 有 INTERNET 权限与 `usesCleartextTraffic=true`。
- 已核实"内存维度为零"：`VEPerfStats.h` 56 处 "memory" 中 55 处是
  `memory_order_relaxed`，1 处是注释里的英文单词，无任何 RSS/fd 采集。
- **本 feature 解锁 decoder-starve-wake-dedup 步骤4**（该步骤长期挂起，
  缺的正是"能持续制造饥饿的源"，由步骤6 的 throttle-below 提供）。
