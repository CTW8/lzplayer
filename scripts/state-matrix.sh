#!/usr/bin/env bash
# 状态机遍历（测试能力 · 状态维度）。
#
# 从每个状态发每个命令，断言"被接受/被拒绝"与代码里的守卫白名单一致。
#
# 为什么值得做：`stop→play` 那个真 bug 本质就是**遍历表里一个格子没人看过**
# ——start() 的白名单是 PREPARED|PAUSED|PLAYBACK_COMPLETE，STOPPED 不在其中，
# 于是 stop 后点播放静默失败、Java 侧丢弃返回值、无痕可查。
#
# 判据来源是双向的：
#   代码侧  VEPlayerDriver 各命令的 `currentState != X && ...` 守卫
#   实测侧  ALOGW 留痕（"state N -> M" 与 "rejected, currentState=N"）
# 两者不符即报差异——**不预设哪边对**，差异本身就是要看的东西。
#
# 用法: ./scripts/state-matrix.sh [素材]
set -uo pipefail

ASSET="${1:-/sdcard/Movies/base-h264-1080p.mp4}"
PKG=com.example.lzplayer
ACT=$PKG/.console.ConsoleActivity
OUT="test-reports/state-matrix-$(date +%Y-%m-%d)"
DEV_LOG=/sdcard/state-matrix.txt
mkdir -p "$OUT"

adb devices | grep -q "device$" || { echo "无设备"; exit 1; }

# 代码侧白名单（与 VEPlayerDriver 的守卫一一对应，改代码时这里必须同步改）
# 格式: 命令|允许的状态名（空格分隔）
WHITELIST="
setDataSource|IDLE
prepareAsync|STOPPED INITIALIZED
start|PREPARED PAUSED PLAYBACK_COMPLETE
stop|PREPARED STARTED PAUSED PLAYBACK_COMPLETE
pause|STARTED
"

echo "== 状态机遍历 =="
echo "   素材 $ASSET"
echo ""
echo "代码侧白名单（来自 VEPlayerDriver 守卫）:"
echo "$WHITELIST" | grep -v '^$' | while IFS='|' read -r cmd states; do
    printf "  %-14s %s\n" "$cmd" "$states"
done

# —— 实测：走一条覆盖多数状态的路径，逐步记录留痕 ——
# 只用 intent 与 UI 命令驱动，不注入、不改 app —— 遍历要验的是产品行为
adb shell "pkill -f 'logcat -f /sdcard/state-matrix'" >/dev/null 2>&1
sleep 1
adb shell "rm -f $DEV_LOG; logcat -c; nohup logcat -f $DEV_LOG >/dev/null 2>&1 &" >/dev/null 2>&1
adb shell am force-stop $PKG >/dev/null 2>&1

echo ""
echo "== 实测路径 =="
step() {
    echo "  $1"
    shift
    adb shell "$@" >/dev/null 2>&1
    sleep "${SLEEP:-4}"
}

# UI 坐标来自实测截图(1240x2772 竖屏): 播放/暂停圆钮在左下, 停止在右下红字。
# 用 UI 而非 intent 驱动后续命令 —— intent 每次都会重开播放器, 把状态冲回
# IDLE, 那样永远测不到"从 STARTED 发 stop"这类格子(第一版就栽在这里:
# seek 用例实测走的是 IDLE→...→STARTED 的完整重开, 而非 STARTED 态下 seek)
TAP_PLAY="input tap 86 2400"
TAP_STOP="input tap 1140 2650"

step "起播（IDLE → INITIALIZED → PREPARING → PREPARED → STARTED）" \
     "am start -n $ACT -e source $ASSET --ez autoplay true"
SLEEP=3 step "pause（STARTED → PAUSED）"        "$TAP_PLAY"
SLEEP=3 step "start（PAUSED → STARTED）"        "$TAP_PLAY"
SLEEP=3 step "stop（STARTED → STOPPED）"        "$TAP_STOP"
SLEEP=3 step "start（STOPPED 态，应被拒绝）"     "$TAP_PLAY"
SLEEP=5 step "prepareAsync（STOPPED → PREPARING）—— 重新打开同一片源" \
     "am start -n $ACT -e source $ASSET --ez autoplay false"
SLEEP=3 step "stop（PREPARED → STOPPED）"       "$TAP_STOP"

# PLAYBACK_COMPLETE 相关的格子要播到片尾才能到达。用极短素材(1.5 秒)
# 而不是等长片播完 —— 遍历的价值在覆盖格子, 不在耗时
TINY=/sdcard/Movies/noaudio-tiny.mp4
SLEEP=6 step "播到片尾（→ PLAYBACK_COMPLETE）" \
     "am start -n $ACT -e source $TINY --ez autoplay true"
SLEEP=3 step "start（PLAYBACK_COMPLETE → STARTED）—— 重播" "$TAP_PLAY"
SLEEP=6 step "再播到片尾" "true"
SLEEP=3 step "stop（PLAYBACK_COMPLETE → STOPPED）"       "$TAP_STOP"

# 最后两格
SLEEP=6 step "重开 → 起播" "am start -n $ACT -e source $ASSET --ez autoplay true"
SLEEP=3 step "pause（→ PAUSED）"                "$TAP_PLAY"
SLEEP=3 step "stop（PAUSED → STOPPED）"         "$TAP_STOP"
# prepareAsync 从 STOPPED: 停止后不换源、直接再打开同一片源
SLEEP=6 step "prepareAsync（STOPPED → PREPARING）" \
     "am start -n $ACT -e source $ASSET --ez autoplay false"

adb shell "pkill -f 'logcat -f /sdcard/state-matrix'" >/dev/null 2>&1
sleep 1
adb pull "$DEV_LOG" "$OUT/logcat.txt" >/dev/null 2>&1
adb shell "rm -f $DEV_LOG" >/dev/null 2>&1

echo ""
echo "== 留痕 =="
# 同时收 Java 侧的上层拦截: 有些格子在到达 native 之前就被 UI 层挡住了
# (如 STOPPED 态点播放, 由 stoppedNeedsReopen 拦截并提示"需重新打开片源")。
# 只看 native 留痕会把这类**正确的产品行为**误判为"未覆盖"
grep -aoE "VEPlayerDriver::[a-zA-Z]+ (state [0-9]+ -> [0-9]+|rejected, currentState=[0-9]+)|START_REJECTED[^\"]{0,30}" \
    "$OUT/logcat.txt" 2>/dev/null | tee "$OUT/transitions.txt" | sed 's/^/  /'

python3 - "$OUT" <<'PY'
import io, os, re, sys
out = sys.argv[1]
NAMES = {0: "ERROR", 1: "IDLE", 2: "INITIALIZED", 4: "PREPARING", 8: "PREPARED",
         16: "STARTED", 32: "PAUSED", 64: "STOPPED", 128: "PLAYBACK_COMPLETE"}
lines = io.open(os.path.join(out, "transitions.txt"), errors="ignore").read().splitlines()

seen_ok, seen_rej = set(), set()
for L in lines:
    m = re.match(r"VEPlayerDriver::(\w+) state (\d+) -> (\d+)", L)
    if m:
        seen_ok.add((m.group(1), int(m.group(2))))
        continue
    m = re.match(r"VEPlayerDriver::(\w+) rejected, currentState=(\d+)", L)
    if m:
        seen_rej.add((m.group(1), int(m.group(2))))

print("")
print("== 覆盖情况 ==")
print("  接受的 (命令, 起始状态):")
for cmd, st in sorted(seen_ok):
    print("    %-14s %s" % (cmd, NAMES.get(st, st)))
if seen_rej:
    print("  被拒绝的:")
    for cmd, st in sorted(seen_rej):
        print("    %-14s %s" % (cmd, NAMES.get(st, st)))
else:
    print("  被拒绝的: 无")

# 未覆盖格子：本次路径没走到的组合。**这才是重点** ——
# stop→play 那个 bug 就藏在没人走过的格子里
WL = {
    "setDataSource": {1},
    "prepareAsync": {64, 2},
    "start": {8, 32, 128},
    "stop": {8, 16, 32, 128},
    "pause": {16},
}
missing = []
for cmd, allowed in WL.items():
    for st in allowed:
        if (cmd, st) not in seen_ok:
            missing.append((cmd, NAMES.get(st, st)))
print("")
print("== 白名单里未被覆盖的格子（%d 个）==" % len(missing))
for cmd, st in sorted(missing):
    print("    %-14s %s" % (cmd, st))
print("")
print("这些格子代码说\"应当接受\"，但本次路径没走到，因此**没有证据**。")
print("stop→play 那个真 bug 就来自这类未验证的格子。")

ui_blocked = [L for L in lines if "START_REJECTED" in L]
if ui_blocked:
    print("")
    print("== 上层拦截（未到达 native，属正确产品行为）==")
    for L in ui_blocked[:5]:
        print("    " + L.strip())
    print("  这类格子 native 守卫是第二道防线，UI 层已先行拦截并给出提示。")
PY

echo ""
echo "== 产物 $OUT =="
