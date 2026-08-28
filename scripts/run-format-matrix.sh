#!/usr/bin/env bash
# 格式矩阵验证（测试能力 · 格式维度）。
#
# 逐个播放 gen-format-assets.sh 生成的素材，断言能出帧、无错误。
#
# 判据要能区分两件事：
#   "播放器不支持这个格式"  —— 真问题
#   "素材本身有问题"        —— 测试的问题
# 所以每个素材播放前先看 ffprobe 校验结果（生成时已做），播放后看 VESTAT
# 与错误留痕。**不能只看"有没有崩"**——不支持的格式往往安静地什么都不做。
set -uo pipefail

PKG=com.example.lzplayer
ACT=$PKG/.console.ConsoleActivity
OUT="test-reports/format-$(date +%Y-%m-%d)"
DEV_LOG=/sdcard/format.txt
mkdir -p "$OUT"

adb devices | grep -q "device$" || { echo "无设备"; exit 1; }

ASSETS="
aud-aac-8000hz.mp4|采样率 8kHz
aud-aac-22050hz.mp4|采样率 22.05kHz
aud-aac-44100hz.mp4|采样率 44.1kHz（CD）
aud-aac-48000hz.mp4|采样率 48kHz（视频标准）
aud-aac-96000hz.mp4|采样率 96kHz（高解析）
aud-aac-mono.mp4|单声道
aud-aac-5_1.mp4|5.1 声道（验下混）
aud-mp3.mp4|mp3 编码
aud-flac.mkv|flac 编码（mkv）
ctn-h264-aac.mkv|容器 mkv
ctn-h264-aac.ts|容器 ts
"

printf "%-24s %-22s %-6s %-6s %s\n" "素材" "轴" "fps" "VESTAT" "判定"

pass=0; fail=0
# **不能用 `... | while read`**: 循环体里的 adb 会吃掉标准输入, 第一轮之后
# read 就读不到东西, 循环静默结束 —— 实测 11 个素材只跑了 1 个。
# 改为先读进数组, 循环体内不依赖 stdin。
IFS=$'\n' read -r -d '' -a ROWS < <(echo "$ASSETS" | grep -v '^$' && printf '\0')
for row in "${ROWS[@]}"; do
    asset="${row%%|*}"; desc="${row#*|}"
    adb shell "pkill -f 'logcat -f /sdcard/format'" >/dev/null 2>&1
    sleep 1
    adb shell "rm -f $DEV_LOG; logcat -c; nohup logcat -f $DEV_LOG >/dev/null 2>&1 &" >/dev/null 2>&1
    adb shell am force-stop $PKG >/dev/null 2>&1
    adb shell "am start -n $ACT -e source /sdcard/Movies/$asset --ez autoplay true" >/dev/null 2>&1
    sleep 12
    adb shell "pkill -f 'logcat -f /sdcard/format'" >/dev/null 2>&1
    sleep 1
    adb pull "$DEV_LOG" "$OUT/$asset.log" >/dev/null 2>&1

    L="$OUT/$asset.log"
    # 只看本应用的日志（logcat 全局，系统媒体组件会污染信号）
    fps=$(grep -aoE "VESTAT t=[0-9]+ fps=[0-9.]+" "$L" 2>/dev/null | tail -1 | sed 's/.*fps=//')
    n=$(grep -ac VESTAT "$L" 2>/dev/null)
    err=$(grep -acE "demux open failed|ON_ERROR|state [0-9]+ -> 0\b" "$L" 2>/dev/null)
    [ -z "$fps" ] && fps="-"

    # 判定：要出帧（VESTAT >= 3）且无错误。
    # **不支持的格式往往安静地什么都不做**，所以"没崩"不算通过
    if [ "$n" -ge 3 ] && [ "$err" -eq 0 ]; then
        verdict="PASS"
    elif [ "$err" -gt 0 ]; then
        verdict="FAIL(错误)"
    else
        verdict="FAIL(无帧)"
    fi
    printf "%-24s %-22s %-6s %-6s %s\n" "$asset" "$desc" "$fps" "$n" "$verdict"
done

adb shell "rm -f $DEV_LOG" >/dev/null 2>&1
echo ""
echo "== 产物 $OUT =="
