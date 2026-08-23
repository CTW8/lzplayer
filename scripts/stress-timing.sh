#!/usr/bin/env bash
# 时序压力测试（测试能力 · 时序维度）。
#
# **本项目已确认的真 bug 几乎全是时序类**：
#   EOF 哨兵被当普通帧吃掉      1/3 概率，加日志即消失（Heisenbug）
#   buffering START/END 乱序     多线程 readAt 下 END 抢在 START 前
#   readAt 等待循环永久卡死      左沿被另一线程推过自己的 offset
#   reportFatal 二次上报         旧实例被弃用后仍发言
# 而现有测试全是"单动作 → 等稳定 → 看数字"，**从不制造并发压力**——
# 这些 bug 能被发现全靠运气或故障注入，不是靠测试设计。
#
# 三种压力：
#   seek 风暴    步距压到 200ms，触发 seek 抢占（abort() 路径从未被真实走过）
#   命令抖动     play/pause 快速交替，压状态机与组件握手
#   负载注入     背景 CPU 占满，改变所有时序关系（竞态概率随之变化）
#
# 判据不是"跑完不崩"，而是**留痕自洽**：
#   状态迁移无非法跳转、无未配对的 buffering、无 dropOvf（credit 记账失守）、
#   无 ATE EOF sentinel（那条常驻探针修好后应永不触发）
set -uo pipefail

ASSET="${1:-/sdcard/Movies/base-h264-1080p.mp4}"
ROUNDS="${2:-3}"
PKG=com.example.lzplayer
ACT=$PKG/.console.ConsoleActivity
OUT="test-reports/stress-$(date +%Y-%m-%d)"
DEV_LOG=/sdcard/stress.txt
mkdir -p "$OUT"

adb devices | grep -q "device$" || { echo "无设备"; exit 1; }

TAP_PLAY="input tap 86 2400"

start_log() {
    adb shell "pkill -f 'logcat -f /sdcard/stress'" >/dev/null 2>&1
    sleep 1
    adb shell "rm -f $DEV_LOG; logcat -c; nohup logcat -f $DEV_LOG >/dev/null 2>&1 &" >/dev/null 2>&1
}
stop_log() {
    adb shell "pkill -f 'logcat -f /sdcard/stress'" >/dev/null 2>&1
    sleep 1
    adb pull "$DEV_LOG" "$1" >/dev/null 2>&1
    adb shell "rm -f $DEV_LOG" >/dev/null 2>&1
}

# —— 场景 1: seek 风暴 ——
# 步距 200ms 远小于实测 seek 耗时(131~272ms)，必然产生抢占；
# 而 VESeekTrace::abort() 这条路径此前从未被真实触发过
echo "== seek 风暴（步距 200ms）=="
start_log
adb shell am force-stop $PKG >/dev/null 2>&1
# UI 的 tap/swipe 都不触发 seek(实测 seek=0), SeekBar 拖动在 adb input 下
# 不可靠。改用 harness 的 seekPercents + **可配步距** --ei seekStepMs 200:
# 200ms 远小于实测 seek 耗时(131~272ms), 必然产生抢占
adb shell "am start -n $ACT -e source $ASSET --ez autoplay true \
    --es seekPercents 10,80,20,70,30,60,40,50,15,85,25,75 \
    --ei seekStepMs 200 --ei playSeconds 15" >/dev/null 2>&1
sleep 30
stop_log "$OUT/seek-storm.txt"

# —— 场景 2: 命令抖动 ——
# play/pause 每 300ms 一次，压状态机与组件握手。间隔取 300ms 是因为
# 实测一次 pause 的组件回执握手在 100ms 量级，300ms 能保证前一次刚完成
echo "== 命令抖动（play/pause × $((ROUNDS * 10))）=="
start_log
adb shell am force-stop $PKG >/dev/null 2>&1
adb shell "am start -n $ACT -e source $ASSET --ez autoplay true" >/dev/null 2>&1
sleep 6
for _ in $(seq 1 $((ROUNDS * 10))); do
    adb shell "$TAP_PLAY" >/dev/null 2>&1
    sleep 0.3
done
sleep 4
stop_log "$OUT/cmd-jitter.txt"

# —— 场景 3: 负载注入 ——
# 背景 CPU 占满会改变所有线程的调度时序，竞态概率随之变化。
# EOF 哨兵那个 bug 就是"加日志即消失"的类型，负载注入是反向手段
echo "== 负载注入（背景占满 CPU）=="
start_log
adb shell am force-stop $PKG >/dev/null 2>&1
NPROC=$(adb shell "cat /proc/cpuinfo | grep -c processor" 2>/dev/null | tr -d '\r')
[ -z "$NPROC" ] && NPROC=4
for _ in $(seq 1 "$NPROC"); do
    adb shell "nohup sh -c 'while true; do :; done' >/dev/null 2>&1 &" >/dev/null 2>&1
done
adb shell "am start -n $ACT -e source $ASSET --ez autoplay true --ei playSeconds 25" >/dev/null 2>&1
sleep 32
adb shell "pkill -f 'while true'" >/dev/null 2>&1
stop_log "$OUT/cpu-load.txt"

# —— 判据：留痕自洽 ——
python3 - "$OUT" <<'PY'
import io, os, re, sys
out = sys.argv[1]
NAMES = {0: "ERROR", 1: "IDLE", 2: "INITIALIZED", 4: "PREPARING", 8: "PREPARED",
         16: "STARTED", 32: "PAUSED", 64: "STOPPED", 128: "PLAYBACK_COMPLETE"}

print("")
print("== 判据（留痕自洽，不是'跑完不崩'）==")
bad = 0
for name in ("seek-storm", "cmd-jitter", "cpu-load"):
    p = os.path.join(out, name + ".txt")
    if not os.path.exists(p):
        continue
    t = io.open(p, errors="ignore").read()

    # buffering 必须成对：多线程下这条最容易被打破（已修过一次乱序）
    bs = t.count("buffering, pausing data flow")
    be = t.count("buffering done, resuming")
    # credit 记账失守：出现即 bug，非"正常但少见"
    ovf = sum(int(m) for m in re.findall(r"dropOvf=(\d+)", t))
    # 常驻回归探针：修好后应永不触发
    ate = t.count("ATE EOF sentinel")
    # 非法迁移：任何进入 ERROR 态都要看
    err = len(re.findall(r"state \d+ -> 0\b", t))
    seeks = t.count("seek stage 1/3")
    aborts = t.count("ignored during seek") + t.count("abortSeek")

    issues = []
    if abs(bs - be) > 1:
        issues.append("buffering 未配对 %d/%d" % (bs, be))
    if ovf:
        issues.append("dropOvf=%d（credit 记账失守）" % ovf)
    if ate:
        issues.append("ATE EOF sentinel ×%d（回归！）" % ate)
    if err:
        issues.append("进入 ERROR 态 ×%d" % err)

    # **压力自检**: 没施加压力的 PASS 是假通过 —— 本项目在"注入什么都没测到
    # 却报 PASS"上栽过三次。seek 风暴要求真的发生 seek, 命令抖动要求状态真的
    # 在迁移, 负载注入要求播放确实在跑
    activity = {"seek-storm": seeks, "cmd-jitter": len(re.findall(r"state \d+ -> \d+", t)),
                "cpu-load": t.count("VESTAT")}.get(name, 1)
    if activity < 5:
        issues.append("压力未施加(活动量 %d < 5), PASS 无意义" % activity)

    status = "FAIL" if issues else "PASS"
    if issues:
        bad += 1
    print("  %-12s %s  seek=%d abort=%d buffering=%d/%d" %
          (name, status, seeks, aborts, bs, be))
    for i in issues:
        print("      !! " + i)

print("")
print("  seek 风暴的意义: 步距 200ms < 实测 seek 耗时(131~272ms), 必然抢占,")
print("  而 VESeekTrace::abort() 这条路径此前从未被真实触发过。")
sys.exit(1 if bad else 0)
PY
rc=$?
echo ""
echo "== 产物 $OUT =="
exit $rc
