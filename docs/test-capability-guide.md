# LZPlayer 测试能力使用指南

> 更新: 2026-08-22
> 面向: 要验证播放器某个能力、或要新增一条测试的人

## 一、现有能力全景

| 维度 | 手段 | 判据来源 |
|---|---|---|
| 启播 | `VEStartupTrace` T0~T8 分段 | 分段之和与总耗时对账 |
| 稳态播放 | `VESTAT` 逐秒时间线 | 分段统计 + 双来源交叉校验 |
| 插桩覆盖率 | `VEGAUGE` | 分段 CPU vs 线程总量 |
| 渲染开销 | `VERENDER` | upload/draw/swap + 窗口外 CPU |
| seek | `VESeekTrace` 三阶段 + 精度 | 精度对 ffprobe 关键帧 |
| 内存/fd | `VESTAT` 的 `rssMb`/`fd` 列 | 稳态斜率（非绝对值） |
| **画面正确性** | `assert-visual.sh` 四角色块 | 像素比对 |
| 网络播放 | `media-server.py` + 场景矩阵 | 服务器字节日志交叉校验 |
| 跑分报告 | `run-benchmark.sh` + `gen-report.py` | 口径写死在生成器里 |
| **回归检测** | `compare-reports.py` | 指纹必须一致才比；判据翻转单独列出 |

## 二、常用命令

```bash
# 1. 生成/重建测试素材（合成矩阵）
./scripts/gen-test-assets.sh --push

# 2. 普查本机真实素材并挑选（网络测试用）
./scripts/scan-assets.sh ~/Downloads assets/serving

# 3. 单个用例跑分（本地源）
./scripts/run-benchmark.sh base-h264-1080p.mp4 true 40 25,75 base-sw
./scripts/gen-report.py test-reports/raw/base-sw

# 4. 网络播放（先起服务器 + 反向端口）
./scripts/media-server.py 8188 assets/serving &
adb reverse tcp:8188 tcp:8188
./scripts/run-benchmark.sh "http://127.0.0.1:8188/long-53min.mp4" true 60 "" net-sw

# 5. 网络场景矩阵（12 个场景一次跑完）
./scripts/run-net-matrix.sh

# 6. 画面正确性断言（不能与上面并行）
./scripts/assert-visual.sh "http://127.0.0.1:8188/probe-visual.mp4"

# 7. 跨轮次对照（回归检测）—— 判据 PASS→FAIL 或出现回退时退出码非零，可接 CI
./scripts/compare-reports.py 基线目录 新目录
./scripts/compare-reports.py "a1,a2,a3" "b1,b2,b3"   # N>=3 取中位数
```

## 三、注入参数（网络场景）

经 URL query 传入，与用例绑定：

| 参数 | 含义 | 取值依据 |
|---|---|---|
| `kbps=1440` | 限速 | 按素材码率的倍数定。0.6x 约 7 秒进入饥饿 |
| `ttfb=2` | 首字节延迟（秒） | 验网络等待是否落在启播 T1/T2 段 |
| `stall=6@0.25` | 送出 0.25MB 后断流 6 秒 | **按字节不按秒**，见下 |
| `norange=1` | 强制 200 全量 | 验 Range 不支持时的降级 |

**`&` 必须让整条 `am start` 命令带引号**，否则被 adb shell 当成后台运行符，注入参数静默丢失。

## 四、四个必须避开的坑

这些不是理论风险，每一个都真实发生过并产生了看似合理的错误结论。

**1. 测量工具污染被测对象**

宿主机流式 `adb logcat > file &` 会与 `adb reverse` 抢同一条 USB 通道，实测把 fps 从 23.9 压到 1.4。而且伪装得极好——数据完整、内存斜率正常，**只有 fps 露馅**。正确做法是设备侧 `logcat -f` 落盘、测后拉取。

**2. 日志采集本身的三种失真**

- 跑完再 `adb logcat -d`：被环形缓冲截断，5 分钟只留最后 103 秒
- 设备侧复用同一文件名：`rm` 不生效（采集进程持有句柄），新旧日志叠加
- 脚本互相 kill：`assert-visual.sh` 的 `force-stop` 会打断长稳

**3. 注入"什么都没测到"却报 PASS**

`VEBufferedDataSource` 默认 32MB 缓存，低码率素材上能撑 2~12 分钟（见 manifest 的 `cache_drain_sec`）。**不限速时断流 5 秒播放器根本察觉不到**。stall 类场景必须叠加限速，且触发点按已送出字节而非秒——按秒定与限速叠加时永远撞不上播放窗口。

**4. 拿参数推算代替读实测值**

项目规则：**涉及媒体素材的判断必须先 `ffprobe`/`ls` 读实际结构**。已栽过两次：把 `90000/3001` 当成 VFR（实际算出来 29.99 恒定）、拿码率推算文件大小得出"偏移越界"的错误结论（实际文件 964MB）。

## 五、统计口径（写死在 `gen-report.py`，不交给读报告的人）

- 所有逐秒列**整段统计**给 n/mean/p50/p95/max，**禁止只报某一行**——CPU 列单次运行内方差达 30 个百分点，挑几行能得出相反结论
- 窗口**显式分段**：起播段 / 稳态段 / 每次 seek 后 2 秒，不许混算
- `n < 30` 的分位数发 `--`（与 `VEPerfHistogram::kMinSamples` 一致）
- 泄漏判据样本 `< 120 秒` 判 INCONCLUSIVE，不硬算斜率
- 哨兵值（`-1` / `-9999`）统计前剔除并记录剔除数
- 指纹缺任何一项 → **拒绝出报告**而不是留空
- 判据缺交叉校验来源 → INCONCLUSIVE，不许算 PASS

## 六、素材选用

**性能对照只能用四个基线素材**（`base-h264-1080p` / `base-hevc-1080p` / `high-4k` / `high-fps`），且**必须 60 秒以上**——10 秒素材扣掉起播与 seek 窗口后稳态凑不满 30 样本，分位数全是 `--`。

行为素材（无音轨 / 纯音频 / 不等长 / VFR / 竖屏 / 双音轨）**不得用于性能基线**，跨素材数字不可比。

## 七、尚未覆盖的维度

| 维度 | 阻塞点 |
|---|---|
| 运行期硬解 fallback | 缺故障注入（`decoder-test-redesign.md` §2 已设计、未实现） |
| 损坏码流容错 | 同上 |
| 状态机遍历 | 判据已就绪（`VEPlayerDriver` 留痕），测试未建 |
| 时序压力（seek 风暴 / 负载注入） | 未做 |
| 生命周期（后台/锁屏/旋转/换源） | 未做 |
| 音频格式轴、非 mp4 容器 | 素材未生成 |
| A/V 同步的外部地面真值 | 需帧号烧录，本机 ffmpeg 缺 libfreetype |
