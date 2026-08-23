#!/usr/bin/env bash
# 生命周期矩阵（测试能力 · 生命周期维度）。
#
# 为什么值得做：
# 1. 锁屏那次已证明这里有未知行为 —— 当时把"软解不渲染"当成缺陷，实际是设备
#    休眠/keyguard，15580 字节的纯色 PNG 是那次的线索。这类误判源于对生命周期
#    行为没有基线；
# 2. surface 销毁重建正是 MediaCodec 那个平台限制的相邻领域 —— 硬解直出独占
#    surface，而后台/旋转都会销毁它。
#
# 五个场景：
#   background   切后台再回前台
#   lock         锁屏再解锁
#   rotate       横竖屏切换（surface 重建）
#   switch-src   播放中换源
#   open-release open/release 循环 × N（查资源泄漏）
#
# 判据同样是**留痕自洽**，且带压力自检 —— 没真正触发生命周期变化的 PASS
# 是假通过（本项目在这类假 PASS 上栽过三次）。
set -uo pipefail

ASSET="${1:-/sdcard/Movies/base-h264-1080p.mp4}"
ASSET2="${2:-/sdcard/Movies/real-hevc-1080p.mp4}"
PKG=com.example.lzplayer
ACT=$PKG/.console.ConsoleActivity
OUT="test-reports/lifecycle-$(date +%Y-%m-%d)"
DEV_LOG=/sdcard/lifecycle.txt
mkdir -p "$OUT"

adb devices | grep -q "device$" || { echo "无设备"; exit 1; }

start_log() {
    adb shell "pkill -f 'logcat -f /sdcard/lifecycle'" >/dev/null 2>&1
    sleep 1
    adb shell "rm -f $DEV_LOG; logcat -c; nohup logcat -f $DEV_LOG >/dev/null 2>&1 &" >/dev/null 2>&1
}
stop_log() {
    adb shell "pkill -f 'logcat -f /sdcard/lifecycle'" >/dev/null 2>&1
    sleep 1
    adb pull "$DEV_LOG" "$1" >/dev/null 2>&1
    adb shell "rm -f $DEV_LOG" >/dev/null 2>&1
}
play() {
    adb shell am force-stop $PKG >/dev/null 2>&1
    adb shell "am start -n $ACT -e source $1 --ez autoplay true" >/dev/null 2>&1
    sleep 6
}

echo "== background: 切后台 5 秒再回前台 =="
start_log; play "$ASSET"
adb shell input keyevent KEYCODE_HOME >/dev/null 2>&1
sleep 5
adb shell "am start -n $ACT" >/dev/null 2>&1
sleep 6
stop_log "$OUT/background.txt"

echo "== lock: 锁屏 5 秒再解锁 =="
start_log; play "$ASSET"
adb shell input keyevent KEYCODE_POWER >/dev/null 2>&1
sleep 5
adb shell input keyevent KEYCODE_POWER >/dev/null 2>&1
sleep 1
# 解锁：上滑。设备无密码时这一步足够
adb shell input swipe 620 2000 620 800 200 >/dev/null 2>&1
sleep 6
stop_log "$OUT/lock.txt"

echo "== rotate: 横竖屏切换（surface 重建）=="
start_log; play "$ASSET"
adb shell settings put system accelerometer_rotation 0 >/dev/null 2>&1
adb shell settings put system user_rotation 1 >/dev/null 2>&1   # 横屏
sleep 6
adb shell settings put system user_rotation 0 >/dev/null 2>&1   # 回竖屏
sleep 6
adb shell settings put system accelerometer_rotation 1 >/dev/null 2>&1
stop_log "$OUT/rotate.txt"

echo "== switch-src: 播放中换源 =="
start_log; play "$ASSET"
adb shell "am start -n $ACT -e source $ASSET2 --ez autoplay true" >/dev/null 2>&1
sleep 8
stop_log "$OUT/switch-src.txt"

echo "== open-release: open/release 循环 × 5 =="
start_log
for _ in 1 2 3 4 5; do
    adb shell am force-stop $PKG >/dev/null 2>&1
    adb shell "am start -n $ACT -e source $ASSET --ez autoplay true" >/dev/null 2>&1
    sleep 4
done
sleep 3
stop_log "$OUT/open-release.txt"

python3 - "$OUT" <<'PY'
import io, os, re, sys
out = sys.argv[1]

# 每个场景的"压力自检"信号：没触发生命周期变化的 PASS 是假通过
SIGNAL = {
    "background":   ("surface 变化", r"onSurfaceChanged|setSurface|surfaceDestroyed"),
    "lock":         ("surface 变化", r"onSurfaceChanged|setSurface|surfaceDestroyed"),
    "rotate":       ("surface 变化", r"onSurfaceChanged|setSurface|surfaceDestroyed"),
    "switch-src":   ("换源",         r"setDataSource state"),
    "open-release": ("重复起播",     r"setDataSource state"),
}

print("")
print("== 判据（留痕自洽 + 压力自检）==")
bad = 0
for name, (sig_name, sig_re) in SIGNAL.items():
    p = os.path.join(out, name + ".txt")
    if not os.path.exists(p):
        continue
    t = io.open(p, errors="ignore").read()

    sig = len(re.findall(sig_re, t))
    bs = t.count("buffering, pausing data flow")
    be = t.count("buffering done, resuming")
    ovf = sum(int(m) for m in re.findall(r"dropOvf=(\d+)", t))
    ate = t.count("ATE EOF sentinel")
    err = len(re.findall(r"state \d+ -> 0\b", t))
    vestat = t.count("VESTAT")
    # 崩溃/ANR 在任何场景下都是硬失败
    fatal = t.count("FATAL EXCEPTION") + t.count("ANR in " )

    issues = []
    if sig < 1:
        issues.append("%s 未发生（信号 0），PASS 无意义" % sig_name)
    if abs(bs - be) > 1:
        issues.append("buffering 未配对 %d/%d" % (bs, be))
    if ovf:
        issues.append("dropOvf=%d（credit 记账失守）" % ovf)
    if ate:
        issues.append("ATE EOF sentinel ×%d（回归！）" % ate)
    if fatal:
        issues.append("崩溃/ANR ×%d" % fatal)

    status = "FAIL" if issues else "PASS"
    if issues:
        bad += 1
    print("  %-14s %s  %s=%d  VESTAT=%d  err态=%d" %
          (name, status, sig_name, sig, vestat, err))
    for i in issues:
        print("      !! " + i)

print("")
print("  注: err态(进入 ERROR)在换源/循环场景下可能是正常的清理路径，")
print("  故只报数不判失败 —— 判失败的是崩溃、buffering 失配、credit 失守。")
sys.exit(1 if bad else 0)
PY
rc=$?
echo ""
echo "== 产物 $OUT =="
exit $rc
