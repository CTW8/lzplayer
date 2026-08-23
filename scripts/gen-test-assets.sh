#!/usr/bin/env bash
# 生成 docs/decoder-test-redesign.md §3 的测试素材矩阵。
#
# 为什么是脚本而不是把文件入库：4K/60fps 素材体积大，入库会把仓库撑爆，
# 而且素材参数一旦写死在二进制里就没人知道它当初是怎么生成的。
#
# 用法:  ./scripts/gen-test-assets.sh [输出目录]
#        ./scripts/gen-test-assets.sh --push        生成后推到设备
set -euo pipefail

# 默认放工程内 assets/ 而非 /tmp: /tmp 会被系统清理, 素材没了之后
# gen-report 会因拿不到 ffprobe 指纹而拒绝出报告(设计如此), 但原因隐蔽
OUT="${1:-assets/generated}"
PUSH=0
if [ "${1:-}" = "--push" ]; then OUT=assets/generated; PUSH=1; fi
DEV=/sdcard/Movies
mkdir -p "$OUT"

# 合成源的选取：**不能用纯色块或静止图**。这类内容压缩后近乎为零，解码
# 几乎不耗 CPU，拿它测出来的性能数字会系统性地低估真实负载——这个项目
# 已经吃过一次亏：640x360 合成素材上 find_stream_info 只要 2~4ms，换到
# 1080p 真实素材是 133~145ms，瓶颈排序整个变了。
#
# testsrc2 提供运动与细节，叠加 noise 进一步抬高熵值，使码率和解码成本
# 落在真实拍摄素材的量级上。
src() {   # src <宽x高> <帧率> <秒>
  echo "-f lavfi -i testsrc2=size=$1:rate=$2:duration=$3 \
        -f lavfi -i sine=frequency=440:duration=$3"
}

enc() {   # enc <输出> <编码器> <码率> <额外滤镜>
  echo "生成 $1"
  # shellcheck disable=SC2086
  ffmpeg -y -loglevel error $2 \
    -vf "noise=alls=12:allf=t+u,format=yuv420p" \
    -c:v "$3" -b:v "$4" -g 60 -pix_fmt yuv420p \
    -c:a aac -b:a 128k -ac 2 -ar 44100 \
    -movflags +faststart "$OUT/$1"
}

# —— 四个性能基线素材。所有跨轮次的性能对照只能用这四个 ——
#
# 时长 60s 而不是 10s: 逐秒时间线的稳态段要凑够 30 个样本才给分位数
# (同 VEPerfHistogram::kMinSamples)。10s 素材扣掉起播 2s 与每次 seek 后 2s,
# 稳态只剩个位数, 分位数全是 "--"、判据只能给 INCONCLUSIVE —— 实测过,
# 这不是保守设置而是硬门槛。行为素材不受影响, 它们只验行为不作性能基线。
BASE_SEC=60
enc base-h264-1080p.mp4 "$(src 1920x1080 30 $BASE_SEC)" libx264 20M
enc base-hevc-1080p.mp4 "$(src 1920x1080 30 $BASE_SEC)" libx265 20M
enc high-4k.mp4         "$(src 3840x2160 30 $BASE_SEC)" libx264 40M
enc high-fps.mp4        "$(src 1920x1080 60 $BASE_SEC)" libx264 25M

# —— 行为素材：只验行为，不得用于性能对照 ——
enc tiny-360p.mp4       "$(src 640x360   25 10)" libx264 1M

echo "生成 noaudio.mp4"
ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=30:duration=10 \
  -vf "noise=alls=12:allf=t+u,format=yuv420p" \
  -c:v libx264 -b:v 20M -g 60 -an -movflags +faststart "$OUT/noaudio.mp4"

echo "生成 audioonly.m4a"
ffmpeg -y -loglevel error -f lavfi -i sine=frequency=440:duration=10 \
  -c:a aac -b:a 128k -ac 2 -ar 44100 "$OUT/audioonly.m4a"

# 视频 10s、音频 3s：EOS 双链路判定用。两条轨的 EOS 必须都被正确处理，
# 只按音频判 EOS 的实现会在第 3 秒就停掉画面
echo "生成 shortaudio.mp4"
ffmpeg -y -loglevel error \
  -f lavfi -i testsrc2=size=1280x720:rate=30:duration=10 \
  -f lavfi -i sine=frequency=440:duration=3 \
  -vf "noise=alls=12:allf=t+u,format=yuv420p" \
  -c:v libx264 -b:v 8M -g 60 -c:a aac -b:a 128k \
  -movflags +faststart "$OUT/shortaudio.mp4"

echo
echo "=== 生成完毕 ==="
ls -lh "$OUT"

if [ "$PUSH" = "1" ]; then
  echo
  echo "=== 推送到设备 $DEV ==="
  for f in "$OUT"/*; do adb push "$f" "$DEV/" >/dev/null && echo "  推送 $(basename "$f")"; done
  adb shell "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file://$DEV" >/dev/null 2>&1 || true
fi
