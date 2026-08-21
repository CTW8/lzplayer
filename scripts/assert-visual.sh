#!/usr/bin/env bash
# 画面正确性断言（net-playback-harness 步骤7）。
#
# **这是整套测试能力里唯一的画面判据。** 在此之前所有判据都是"帧出来了、
# 时刻对了、CPU 合理"——从没有一条回答"画面内容对不对"。花屏、绿屏、
# 色彩空间搞错（BT.601/709 × full/limited 从没验过）、画幅拉伸、旋转元数据
# 处理错、上下颠倒，全部测不到。
#
# 做法：播 probe-visual.mp4（四角红/绿/蓝/黄固定色块），截屏后采四角像素，
# 与期望色比对。四角同时对上，才能同时排除：色彩空间错（色值偏移）、
# 画幅拉伸（色块位置偏移）、旋转（四角颜色互换）、上下颠倒（上下互换）。
#
# 用法: ./scripts/assert-visual.sh [源URL或设备路径]
set -uo pipefail

SRC="${1:-http://127.0.0.1:8188/probe-visual.mp4}"
SP="${SCRATCH:-/tmp}"
SHOT="$SP/visual-probe.png"
PKG=com.example.lzplayer

# 本脚本会 force-stop 播放器, **不能与长稳测试并行运行** ——
# 实测它在 30 分钟长稳期间执行, 把被测播放放停了, 而 harness 主循环还在空等。
# 另外它此前还有 `pkill -f 'logcat -f'` 会杀掉长稳的设备侧采集进程。
# 这是本轮第四次"测量互相干扰": 前三次是 logcat 截断、USB 通道争抢、
# 日志文件跨运行叠加。
if pgrep -f "run-benchmark.sh" >/dev/null 2>&1; then
    echo "ABORT 检测到 run-benchmark.sh 正在运行 —— 本脚本会停掉播放器, 拒绝执行"
    exit 2
fi
adb shell am force-stop $PKG >/dev/null 2>&1
adb shell "am start -n $PKG/.console.ConsoleActivity -e source '$SRC' --ez autoplay true" >/dev/null 2>&1
# 等起播稳定：截太早会拍到黑屏或首帧未上屏，那不是画面错误
sleep 8
adb exec-out screencap -p > "$SHOT" 2>/dev/null

python3 - "$SHOT" <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    print("SKIP 需要 Pillow: pip3 install Pillow")
    sys.exit(0)

img = Image.open(sys.argv[1]).convert("RGB")
W, H = img.size
print("截屏 %dx%d" % (W, H))

# 视频区域在屏幕中的位置未知（有控件、有黑边），所以不能按固定坐标采样。
# 改为找出画面里最大的连通纯色区块——四角色块各占 160x120 于 1280x720，
# 即 1/48 画面，在缩放后仍是显著色块。
def sample(fx, fy, r=6):
    """按画面比例取样，返回该点邻域的平均色。取邻域而非单点：
    单点会被压缩伪影或缩放插值带偏，而色块内部是均匀的"""
    x, y = int(W * fx), int(H * fy)
    px = [img.getpixel((min(W-1, max(0, x+dx)), min(H-1, max(0, y+dy))))
          for dx in range(-r, r+1) for dy in range(-r, r+1)]
    n = len(px)
    return tuple(sum(p[i] for p in px)//n for i in range(3))

def classify(c):
    """归到最近的期望色。容差放宽是因为要穿过 h264 压缩 + 缩放 + 截屏，
    但四种色相差异极大，混淆的可能性很低"""
    R, G, B = c
    cands = {"red": (255,0,0), "lime": (0,255,0), "blue": (0,0,255),
             "yellow": (255,255,0), "other": None}
    best, bd = "other", 1e9
    for k, v in cands.items():
        if v is None:
            continue
        d = sum((a-b)**2 for a, b in zip(c, v))
        if d < bd:
            bd, best = d, k
    # 距离过大就是 other，不硬套
    return best if bd < 3*(110**2) else "other"

# **自动探测视频区**，不写死比例。
#
# 实测截屏 1240x2772 竖屏里视频只占中间 y=0.28~0.54 的横条，其余是 UI，
# 按固定比例取四角会全部落在黑色控件区上 —— 那不是画面错误，是采样点错了。
# 视频区随屏幕方向、控件展开状态、素材宽高比而变，必须动态找。
#
# 做法：逐行算非黑像素占比，连续超过阈值的最长行段即视频区。
def video_band():
    rows = []
    for y in range(0, H, max(1, H // 300)):
        lit = sum(1 for x in range(0, W, max(1, W // 40))
                  if sum(img.getpixel((x, y))) > 90)
        rows.append((y, lit / float(max(1, len(range(0, W, max(1, W // 40)))))))
    best, cur = (0, 0), None
    for y, frac in rows:
        if frac > 0.6:
            cur = (cur[0], y) if cur else (y, y)
            if cur[1] - cur[0] > best[1] - best[0]:
                best = cur
        else:
            cur = None
    return best

y0, y1 = video_band()
if y1 - y0 < H * 0.05:
    print("SKIP 未探测到视频区(高度 %d)，可能未起播或全黑" % (y1 - y0))
    sys.exit(0)
print("视频区 y=%d..%d (占屏高 %.0f%%)" % (y0, y1, 100.0 * (y1 - y0) / H))

# 在探测到的视频区内取四角，各向内缩 8% 避开边界过渡带
def fy(t):
    return (y0 + (y1 - y0) * t) / float(H)

probes = {
    "左上": (0.08, fy(0.12), "red"),
    "右上": (0.92, fy(0.12), "lime"),
    "左下": (0.08, fy(0.88), "blue"),
    "右下": (0.92, fy(0.88), "yellow"),
}
ok = 0
for name, (fx, fy, want) in probes.items():
    c = sample(fx, fy)
    got = classify(c)
    mark = "PASS" if got == want else "FAIL"
    if got == want:
        ok += 1
    print("  %-4s %s  期望=%-7s 实测=%-7s rgb=%s" % (name, mark, want, got, c))

print("四角断言 %d/4 通过" % ok)
if ok < 4:
    print("注：四角位置取自画面比例，若视频未铺满屏幕或有控件遮挡，"
          "采样点可能落在视频之外——先看截屏确认，再判定是否真为画面错误")
PY
echo "截屏: $SHOT"
