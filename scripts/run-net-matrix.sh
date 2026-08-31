#!/usr/bin/env bash
# 网络播放场景矩阵（net-playback-harness 步骤6/9）。
#
# 一条命令跑完全部场景并归档。每个场景的注入参数经 URL query 传入，
# 与用例绑定在同一个 URL 上——报告的环境指纹只要记 URL 就完整记录了注入
# 状态，不会出现"忘记关掉上一次注入"导致后续结论作废。
#
# **不能与 assert-visual.sh 并行**：那个脚本会 force-stop 播放器。
#
# 用法: ./scripts/run-net-matrix.sh [输出目录]
set -uo pipefail

OUT="${1:-test-reports/net-matrix-$(date +%Y-%m-%d)}"
BASE="http://127.0.0.1:8188"
SRV_PID=""

cleanup() {
    [ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null
    adb shell "pkill -f 'logcat -f'" >/dev/null 2>&1
}
trap cleanup EXIT

mkdir -p "$OUT"
adb devices | grep -q "device$" || { echo "无设备"; exit 1; }

# 坏内容素材就地生成(体积小、可重现)，不进版本库 —— assets/serving 已 gitignore
[ -f assets/serving/corrupt-random.mp4 ] || \
    head -c 400000 /dev/urandom > assets/serving/corrupt-random.mp4
[ -f assets/serving/corrupt-truncated.mp4 ] || \
    head -c 300000 assets/serving/real-hevc-1080p.mp4 > assets/serving/corrupt-truncated.mp4

pkill -f media-server.py 2>/dev/null
./scripts/media-server.py 8188 assets/serving > /tmp/net-matrix-server.log 2>&1 &
SRV_PID=$!
sleep 2
adb reverse tcp:8188 tcp:8188 >/dev/null 2>&1

# 场景定义：名称 | URL(含注入) | 稳态秒 | seek | 期望
# 注入参数的取值依据写在各行注释里 —— 拍脑袋定的值会让场景"什么都没测到"
# 却报 PASS，本项目已在 stall 场景上栽过三次
run_case() {
    local name="$1" url="$2" secs="$3" seek="$4"
    printf "\n== %s ==\n" "$name"
    rm -rf "test-reports/raw/$name"
    ./scripts/run-benchmark.sh "$url" true "$secs" "$seek" "$name" 2>&1 | tail -3
    ./scripts/gen-report.py "test-reports/raw/$name" > /dev/null 2>&1
    [ -f "test-reports/raw/$name/report.txt" ] && cp "test-reports/raw/$name/report.txt" "$OUT/$name.txt"
}

run_case net-baseline    "$BASE/long-53min.mp4"                        60 ""
run_case net-seek        "$BASE/long-53min.mp4"                        60 "5,25,60"
# 限速 1.5x 码率：理论富余，实测仍因 moov 在文件末尾而反复缓冲
run_case throttle-above  "$BASE/long-53min.mp4?kbps=3600"              60 ""
# 限速 0.6x：(startWater-lowWater)/(码率×0.4) ≈ 7 秒即进入饥饿
run_case throttle-below  "$BASE/long-53min.mp4?kbps=1440"              60 ""
run_case slow-ttfb       "$BASE/long-53min.mp4?ttfb=2"                 40 ""
# 断流点按**已送出字节**而非秒：与限速叠加时按秒定永远撞不上播放窗口。
# 0.25MB 落在 moov 读完之后的主数据早期
run_case stall-recover   "$BASE/long-53min.mp4?kbps=2000&stall=6@0.25" 60 ""
# 永久断流：IO 超时后 demux open 失败是**正确行为**(不能永远挂着)，
# 所以它是期望失败的场景，不该用正常播放判据去判。
EXPECT=error run_case stall-forever "$BASE/long-53min.mp4?kbps=2000&stall=300@0.25" 45 ""
# no-range 用 60s 素材而不是 11s 的 real-hevc-1080p: 后者稳态样本 n=27 < 30,
# 帧率分位数全 `--`、判据只能是 INCONCLUSIVE —— 一个测不出结论的用例
# 与"通过"在汇总表里长得一样。probe-visual 是 60s 且 **moov 在文件尾**,
# 正好逼出无 Range 时"必须先把整个文件拖完才拿得到 moov"的最坏路径。
run_case no-range        "$BASE/probe-visual.mp4?norange=1"             60 "70,20"
# **badcontent.png 不是坏内容。** 一张有效 PNG 会被 FFmpeg 的 png_pipe 当成
# 单帧视频正常解封装、解码、渲染(实测 renderedFrames=1)，然后 EOS 正常完成。
# 此前把它的终态 16 -> 128 读成"进了 STATE_ERROR、错误链路首次被真实错误穿过"
# 是**读反了状态码**: VEPlayerDriver 的枚举是 STATE_ERROR=0、IDLE=1、
# PLAYBACK_COMPLETE=128，16 -> 128 其实是"正常播放完成"。
# 这个用例保留，但它验的是"图片文件按单帧视频播放"，不是错误链路。
run_case image-as-media  "$BASE/badcontent.png"                        20 ""
# 真正的坏内容才验得到错误链路。两种坏法分开：随机字节(连容器都认不出)与
# 截断的真实 mp4(容器头对、数据不全) —— 走的不是同一条失败路径。
EXPECT=error run_case bad-random     "$BASE/corrupt-random.mp4"        20 ""
EXPECT=error run_case bad-truncated  "$BASE/corrupt-truncated.mp4"     20 ""
# 真实轴素材：每条都是合成矩阵没有的维度
run_case portrait-vfr    "$BASE/portrait-vfr-a.mp4"                    20 ""
run_case real-hevc-4k    "$BASE/real-hevc-4k.mp4"                      20 ""
run_case noaudio-odd     "$BASE/noaudio-odd.mp4"                       25 ""

# —— 故障注入场景（需 -PveFaultInject=true 构建，否则 app 会明确告警并返回 -1）——
# 用本地素材: 注入验的是解码器行为, 叠加网络变量只会让失败原因难以归属
FAULT_HW_CREATE=true    run_case fault-hw-create    "/sdcard/Movies/base-h264-1080p.mp4" 25 ""
FAULT_HW_CONFIGURE=true run_case fault-hw-configure "/sdcard/Movies/base-h264-1080p.mp4" 25 ""
FAULT_HW_AFTER=100      run_case fault-hw-runtime   "/sdcard/Movies/base-h264-1080p.mp4" 30 ""

echo ""
echo "== 归档 $OUT =="
ls "$OUT" | tr '\n' ' '
echo ""
