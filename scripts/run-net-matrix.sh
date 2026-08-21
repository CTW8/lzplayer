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
run_case stall-forever   "$BASE/long-53min.mp4?kbps=2000&stall=300@0.25" 45 ""
run_case no-range        "$BASE/real-hevc-1080p.mp4?norange=1"         30 ""
run_case bad-content     "$BASE/badcontent.png"                        20 ""
# 真实轴素材：每条都是合成矩阵没有的维度
run_case portrait-vfr    "$BASE/portrait-vfr-a.mp4"                    20 ""
run_case real-hevc-4k    "$BASE/real-hevc-4k.mp4"                      20 ""
run_case noaudio-odd     "$BASE/noaudio-odd.mp4"                       25 ""

echo ""
echo "== 归档 $OUT =="
ls "$OUT" | tr '\n' ' '
echo ""
