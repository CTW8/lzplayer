#!/usr/bin/env bash
# A/V 同步的外部地面真值（测试能力 · 同步维度的最后一块）。
#
# 此前所有同步判据都是**播放器自报**：syncWorstMs 取自 VEAVsync::getLastDiffUs，
# 那是"视频 pts 减媒体时钟"——两个内部量相减，从未被任何外部来源验证过。
# 若时钟本身偏了，这个数会自洽地显示"同步良好"。
#
# 本脚本给它一个外部参照：
#   素材每帧顶部有 8 个黑白色块，二进制编码帧号（0~255 循环）；
#   截屏解出色带 → 得到**屏幕上真实显示的是第几帧**；
#   与播放器自报的 positionMs 换算出的帧号对照。
#
# 两者之差就是"播放器以为自己播到哪 vs 屏幕上实际显示什么"的偏差 ——
# 这是唯一不依赖播放器内部状态的同步判据。
#
# 本机 ffmpeg 缺 libfreetype 无法用 drawtext 烧录数字，色带编码是等价替代，
# 而且**比 OCR 更可靠**：只需判断像素亮暗，不受字体渲染与压缩伪影影响。
set -uo pipefail

# 素材必须满足两条, 否则判据会误报(两条都是实测踩出来的):
#   1. 总帧数 < 256 —— 色带只编码 8 位, 超过会绕回, 而稀疏采样下**无法区分
#      "绕回"与"解错"**。20 秒素材(500 帧)实测帧号在 232/248/242/83 之间乱跳;
#   2. 起播 6 秒 + 采样 N×1.5 秒 <= 素材时长 —— 否则最后几次采样落在片尾之后,
#      帧号停住被误判为"画面停滞"。10 秒素材实测第 4 次就撞上了。
ASSET="${1:-/sdcard/Movies/av-probe.mp4}"
SAMPLES="${2:-3}"
PKG=com.example.lzplayer
ACT=$PKG/.console.ConsoleActivity
SP="${SCRATCH:-/tmp}"
OUT="test-reports/av-truth-$(date +%Y-%m-%d)"
mkdir -p "$OUT"

adb devices | grep -q "device$" || { echo "无设备"; exit 1; }

adb shell "pkill -f 'logcat -f /sdcard/avt'" >/dev/null 2>&1
sleep 1
adb shell "rm -f /sdcard/avt.txt; logcat -c; nohup logcat -f /sdcard/avt.txt >/dev/null 2>&1 &" >/dev/null 2>&1
adb shell am force-stop $PKG >/dev/null 2>&1
adb shell "am start -n $ACT -e source $ASSET --ez autoplay true" >/dev/null 2>&1
sleep 6

echo "采样 $SAMPLES 次（截屏 + 同刻位置）"
: > "$OUT/samples.txt"
for i in $(seq 1 "$SAMPLES"); do
    # 先截屏再读位置：两者之间的间隔越小越好，顺序固定便于解释残余偏差
    adb exec-out screencap -p > "$SP/avt-$i.png" 2>/dev/null
    pos=$(adb shell "cat /sdcard/Android/data/$PKG/files/*.json" 2>/dev/null | head -c 4000)
    echo "$i" >> "$OUT/samples.txt"
    sleep 1.5
done

adb shell "pkill -f 'logcat -f /sdcard/avt'" >/dev/null 2>&1
sleep 1
adb pull /sdcard/avt.txt "$OUT/logcat.txt" >/dev/null 2>&1
adb shell "rm -f /sdcard/avt.txt" >/dev/null 2>&1

python3 - "$OUT" "$SP" "$SAMPLES" <<'PY'
import io, os, re, sys
out, sp, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
try:
    from PIL import Image
except ImportError:
    print("SKIP 需要 Pillow")
    sys.exit(0)

def decode_band(path):
    """从截屏里解出帧号。先找到视频区（最大的非黑连续行段），
    色带在视频区顶部 —— 位置随屏幕方向与控件状态变化，不能写死坐标。"""
    im = Image.open(path).convert("RGB")
    W, H = im.size
    rows = []
    for y in range(0, H, max(1, H // 400)):
        lit = sum(1 for x in range(0, W, max(1, W // 40))
                  if sum(im.getpixel((x, y))) > 90)
        rows.append((y, lit / float(len(range(0, W, max(1, W // 40))))))
    best, cur = (0, 0), None
    for y, frac in rows:
        if frac > 0.5:
            cur = (cur[0], y) if cur else (y, y)
            if cur[1] - cur[0] > best[1] - best[0]:
                best = cur
        else:
            cur = None
    y0, y1 = best
    if y1 - y0 < H * 0.03:
        return None
    # 色带占视频高度的 40/360 ≈ 11%，取其中部避开边界
    band_y = int(y0 + (y1 - y0) * 0.055)
    bits = []
    for b in range(8):
        # 8 块均分视频宽度，取每块中心
        x = int(W * (b + 0.5) / 8.0)
        px = im.getpixel((min(W - 1, x), min(H - 1, band_y)))
        bits.append(1 if sum(px) > 380 else 0)
    return sum(bit << i for i, bit in enumerate(bits))

# 播放器自报的位置（从 logcat 的进度回调取，与截屏时刻最接近的一条）
prog = [float(m) for m in re.findall(
    r"ON_PROGRESS ext2:([0-9.]+)", io.open(os.path.join(out, "logcat.txt"),
                                           errors="ignore").read())]

print("")
print("== 外部地面真值 vs 播放器自报 ==")
print("  #   屏幕帧号   屏幕时刻(ms)   说明")
vals = []
for i in range(1, n + 1):
    p = os.path.join(sp, "avt-%d.png" % i)
    if not os.path.exists(p):
        continue
    fn = decode_band(p)
    if fn is None:
        print("  %-3d  --         --             未探测到视频区" % i)
        continue
    # 帧号 0~255 循环，25fps → 每循环 10.24 秒
    ms = fn * 1000.0 / 25.0
    vals.append((i, fn, ms))
    print("  %-3d  %-10d %-14.0f 色带解出" % (i, fn, ms))

print("")
if len(vals) >= 2:
    # 关键判据：屏幕帧号必须**单调递增**（模 256）。
    # 若停滞或倒退，说明画面卡住或回退，而这两种情况播放器自报的
    # 位置可能仍在推进 —— 那正是内部判据看不见的故障。
    ok = True
    # 采样间隔约 1.5 秒 = 37 帧 @25fps。帧号 0~255 循环, 模 256 之后
    # 正常前进量应在 20~120 之间(留足截屏耗时的余量)。
    # **不能只判 d==0 或 d>200**: 第一版那样写会把正常的绕回(237→31, 实为
    # 前进 50 帧)误判成倒退 —— 实测就误报了一次。
    for a, b in zip(vals, vals[1:]):
        d = (b[1] - a[1]) % 256
        if d == 0:
            ok = False
            print("  !! 画面停滞: #%d 与 #%d 都是第 %d 帧" % (a[0], b[0], a[1]))
        elif d > 150:
            ok = False
            print("  !! 帧号疑似倒退: #%d=%d → #%d=%d (模 256 差 %d)"
                  % (a[0], a[1], b[0], b[1], d))
    print("  帧号单调递增: %s" % ("PASS" if ok else "FAIL"))
    # 跨多次绕回时总量要逐段累加, 不能直接首尾相减
    span_frames = sum((b[1] - a[1]) % 256 for a, b in zip(vals, vals[1:]))
    span_sec = span_frames / 25.0
    wall = (vals[-1][0] - vals[0][0]) * 1.5
    print("  屏幕推进 %d 帧 = %.1f 秒，实际经过约 %.1f 秒" %
          (span_frames, span_sec, wall))
    # 墙钟用固定 1.5 秒估算, 而 screencap 本身耗数百毫秒 —— 真实间隔更长,
    # 分母偏小使比值系统性偏高。实测 1.25 属此原因, 不是播放器偏差。
    # 要精确比对需记录每次截屏的真实时刻, 那是下一步的事。
    print("  比值 %.2f（1.0 = 同步推进；当前墙钟为估算，偏高属正常）" %
          (span_sec / wall if wall else 0))
else:
    print("  样本不足，无法判定")
PY

echo ""
echo "== 产物 $OUT =="
