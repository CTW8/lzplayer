# 网络播放测试服务 实施计划

> 设计文档: ../../docs/net-playback-harness-design.md
> 创建日期: 2026-08-21

## 方案摘要

在宿主机起一个只绑 `127.0.0.1:8188`、支持 Range、可注入（限速/首字节延迟/断流）、
带字节级请求日志的 HTTP 视频服务，经 `adb reverse` 供给设备，
把播放器网络播放路径从**零覆盖**变成 11 个可回归用例。
核心是**服务器日志作为进程外的独立交叉源**：播放器的 `vstarve`/buffering 事件
过去只能自己证明自己，有了服务端"每秒送出多少字节"才够 decoder-test-redesign §4 的双来源门槛。
素材用 `~/Downloads` 真实文件（VFR/竖屏/真实 HEVC/长片/无音轨/奇宽高），**不入库**。

## 关键约束

1. **服务器只绑回环**，写死 `127.0.0.1`，不提供 `--host` 参数。素材是用户私人文件。
2. **serving 目录、manifest、素材副本一律不进 git**，仓库只进脚本。
3. **每条判据双来源**，实测值与交叉校验值缺一即 INCONCLUSIVE，不算 PASS
   （网络场景最容易出现"注入没生效但播得很顺 → 全绿"）。
4. **VFR 素材自动豁免帧率类判据**，manifest 打 `is_vfr` 标记，报告显式声明豁免。
5. **stall/throttle 类场景必须先算缓存排空时间**（见设计文档 §5.1）：
   32MB 缓存在 1080p 下约 50 秒存量，不限速时"断流 5s"什么都不会发生。
   参数不满足触发条件时 harness 须**拒绝执行并报 INCONCLUSIVE**，不许跑出空洞的 PASS。
6. **复用 perf-metrics 步骤7 的 harness 与报告格式**（`scripts/run-benchmark.sh` /
   `scripts/gen-report.py` / `test-reports/raw/<case>/`），不另起一套。
7. **长稳场景不得只依赖 logcat**（ColorOS 300 行/进程配额），须落盘读回。
8. 前置事实已核实：`VESourceRegistry` 已注册 http/https；`ConsoleActivity`
   的 `--es source` 原样透传不校验本地文件存在性；`AndroidManifest` 已有
   INTERNET 权限与 `usesCleartextTraffic=true`。**理论上 URL 能一路走到底，
   但从没验证过 —— 这正是步骤3 单列的原因。**

## 实施步骤

1. **素材普查落盘 + manifest**
   `scripts/scan-assets.sh` 对 `~/Downloads` 跑 ffprobe 全指纹，产
   `assets/manifest.json`（分辨率/编码/帧率/`r_frame_rate` 与 `avg_frame_rate`/
   码率/时长/轨道构成/文件大小/**`is_vfr` 标记**/**缓存排空时间估算**），
   挑选集复制到 serving 目录。`.gitignore` 排除 serving 目录与 manifest。
   验收：manifest 覆盖设计文档 §3 全部 8 类素材 + 48 张图；
   四个 VFR 素材 `is_vfr=true`；两个无音轨素材轨道构成正确；
   `git status` 干净（无素材、无 manifest）。

2. **media-server.py**
   只绑 `127.0.0.1:8188`；完整 Range（206/`Content-Range`/`Accept-Ranges`）
   + `--no-range` 模式；三种注入（`rate`/`ttfb`/`stall`）；`POST /control`
   运行期改参数；字节级日志三类行（REQ/RATE/END，`t=` 用 epoch 毫秒）。
   验收：`curl` 覆盖 Range 起止/越界/`--no-range` 降级；限速实测误差 < 10%；
   `stall` 保持连接不关闭；控制接口改参数即时生效；日志三类行齐全可解析。

3. **接线 smoke（单列一步，失败即是产出）**
   `adb reverse tcp:8188 tcp:8188` + `am start --es source http://127.0.0.1:8188/media/<x>.mp4`，
   播通一个短素材。
   验收：见本文档末"当前工作项"。**失败不阻塞后续步骤设计** ——
   转 bug 排查并把结论写回本 plan 与设计文档。

4. **harness 扩展**
   `run-benchmark.sh` 加 `--url`、场景编排（注入参数序列 + 时序）、
   服务器日志并入 `test-reports/raw/<case>/server.log`、
   **设备与宿主机时钟偏移采集写进 `env.txt`**、
   **缓存排空时间校验**（约束5，不满足则拒绝执行）。
   验收：一条命令跑完 baseline 场景，产物含 server.log 与时钟偏移；
   故意给一组不可能触发饥饿的参数时 harness 拒绝执行并说明原因。

5. **VESTAT 加 RSS/fd 列**（native 小改，longrun 前置）
   现状核实：`VEPerfStats.h` 中 56 处 "memory" 有 55 处是 `memory_order_relaxed`，
   **内存维度为零**。读 `/proc/self/statm` 与 `/proc/self/fd` 计数，
   挂已有 `maybeEmit` 逐秒发差值与绝对值，不新增唤醒源。
   验收：VESTAT 行出现 `rssKb=` 与 `fdCount=`；与
   `adb shell dumpsys meminfo` / `ls /proc/<pid>/fd | wc -l` 交叉校验同量级（双来源）。

6. **场景矩阵逐个跑**（设计文档 §5 的 11 个中除 probe-visual 与 longrun 的 9 个）
   每场景一用例、判据双源。**throttle-below 是本 feature 的核心交付**：
   `vstarve`/`astarve` 首次拿到非零值，buffering 事件链首次被真实事件穿过。
   验收：9 个场景各出一份 raw 产物与判据结论；
   throttle-below 的 `vstarve` 非零且 fps 塌陷区间与服务器字节日志对齐；
   stall-forever 不 ANR 且错误到达 Java 层。

7. **探针素材 + 像素断言**
   `scripts/gen-probe-asset.sh`（四角固定色块 + 中央 drawtext 烧录帧号）；
   `screencap` 取四角像素比对期望 RGB；帧号作 A/V 外部真值。
   验收：正常播放四角像素全部命中（容差内）；
   **须先构造一次已知错误**（如强制错误色彩空间）确认断言**会失败**，
   否则无法区分"画面正确"与"断言根本没在判"。

8. **长稳**
   test2.mp4（53min）× rate=码率×1.2 × 循环，RSS/fd 时间线斜率做泄漏检测。
   验收：全程无崩溃/ANR；RSS 与 fd 斜率在阈值内；
   服务器连接数不单调增长（交叉校验）；数据落盘读回而非 logcat。

9. **基线归档**
   全部结论归档 `test-reports/net-baseline-<date>/`，
   格式复用 perf-metrics 7d；显式声明"经 adb reverse 供给，
   非真实网络条件"；VFR 素材的帧率判据豁免逐条标注。
   验收：报告可被后续轮次直接对照；
   **并在归档中确认 decoder-starve-wake-dedup 步骤4 是否已被本 feature 解锁**。

## 交付物清单（本 feature 完成时应存在）

- `scripts/media-server.py`、`scripts/scan-assets.sh`、`scripts/gen-probe-asset.sh`
- `scripts/run-benchmark.sh` 的网络扩展
- VESTAT 的 `rssKb` / `fdCount` 两列
- `test-reports/net-baseline-<date>/`
- **decoder-starve-wake-dedup 步骤4 解锁**（明确交付物，非副作用）
