#!/usr/bin/env bash
# 音频格式轴与容器轴素材（测试能力 · 格式维度）。
#
# 现有素材矩阵的音频侧只有一种组合：AAC 48kHz 立体声。而播放器要处理的
# 真实世界远不止这一种 —— 采样率跨 8k~96k、声道数 1~6、编码格式 mp3/flac/opus，
# 容器也不止 mp4。这些轴上任何一条走不通，都是用户可见的"打不开"。
#
# 视频侧统一用最小规格（320x240 10 秒 h264），**因为要测的是音频与容器**——
# 视频参数变化会引入无关变量，也会让素材体积失控。
set -uo pipefail

OUT="${1:-assets/format}"
mkdir -p "$OUT"

V="-f lavfi -i testsrc2=size=320x240:rate=25:duration=10"

echo "== 音频格式轴 =="
# 采样率轴：8k 是电话质量下限，96k 是高解析音频上限。
# 中间取 44.1k（CD）与 48k（视频标准）—— 这两个是重灾区：
# 重采样器在它们之间转换时最容易出问题（44100 与 48000 不成整数倍）
for sr in 8000 22050 44100 48000 96000; do
    ffmpeg -y -loglevel error $V -f lavfi -i "sine=frequency=440:duration=10" \
        -c:v libx264 -b:v 500k -c:a aac -ar $sr -ac 2 \
        "$OUT/aud-aac-${sr}hz.mp4" 2>/dev/null && echo "  aac ${sr}Hz"
done

# 声道轴：单声道与 5.1。5.1 要验的是**下混**——设备多为立体声输出，
# 播放器必须把 6 声道正确下混，而不是只放前两个声道或直接失败
ffmpeg -y -loglevel error $V -f lavfi -i "sine=frequency=440:duration=10" \
    -c:v libx264 -b:v 500k -c:a aac -ac 1 -ar 48000 \
    "$OUT/aud-aac-mono.mp4" 2>/dev/null && echo "  aac 单声道"
ffmpeg -y -loglevel error $V \
    -f lavfi -i "sine=frequency=440:duration=10" \
    -c:v libx264 -b:v 500k -c:a aac -ac 6 -ar 48000 \
    "$OUT/aud-aac-5_1.mp4" 2>/dev/null && echo "  aac 5.1"

# 编码格式轴：mp3 与 flac 走的是与 aac 不同的解码器路径
ffmpeg -y -loglevel error $V -f lavfi -i "sine=frequency=440:duration=10" \
    -c:v libx264 -b:v 500k -c:a libmp3lame -ar 44100 -ac 2 \
    "$OUT/aud-mp3.mp4" 2>/dev/null && echo "  mp3"
# flac 在 mp4 里兼容性差，放 mkv —— 这也顺带覆盖了容器轴
ffmpeg -y -loglevel error $V -f lavfi -i "sine=frequency=440:duration=10" \
    -c:v libx264 -b:v 500k -c:a flac -ar 48000 -ac 2 \
    "$OUT/aud-flac.mkv" 2>/dev/null && echo "  flac (mkv)"

echo "== 容器轴 =="
# 同样的音视频内容装进不同容器：差异只在解封装, 便于归因
for fmt in mkv ts; do
    ffmpeg -y -loglevel error $V -f lavfi -i "sine=frequency=440:duration=10" \
        -c:v libx264 -b:v 500k -c:a aac -ar 48000 -ac 2 \
        "$OUT/ctn-h264-aac.$fmt" 2>/dev/null && echo "  $fmt"
done

echo ""
echo "== ffprobe 校验（不校验就不知道生成的是不是想要的）=="
printf "%-26s %-8s %-8s %-6s %s\n" "文件" "音频" "采样率" "声道" "容器"
for f in "$OUT"/*; do
    [ -f "$f" ] || continue
    info=$(ffprobe -v error -select_streams a:0 \
        -show_entries stream=codec_name,sample_rate,channels \
        -show_entries format=format_name -of csv=p=0 "$f" 2>/dev/null | tr '\n' ' ')
    printf "%-26s %s\n" "$(basename "$f")" "$info"
done
echo ""
du -sh "$OUT" | cut -f1 | xargs echo "总体积:"
