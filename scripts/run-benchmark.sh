#!/usr/bin/env bash
# 跑分 harness（perf-metrics 步骤7c）。只负责**采集与完整性校验**，
# 报告渲染是 7d 的事——两者分开，是为了让同一份采集可以反复喂给生成器
# 而得出同一组数字（7e 的口径要求）。
#
# 用法:
#   ./scripts/run-benchmark.sh <素材名> [软解:true|false] [稳态秒] [seek百分比] [用例名]
#   ./scripts/run-benchmark.sh base-h264-1080p.mp4 true 15 10,50,90 base-sw
#
# 产物落 test-reports/raw/<用例名>/ ：
#   timeline.txt   VESTAT/VERENDER/VEGAUGE 原始行
#   snapshot.json  收尾时的一次性快照
#   asset.json     ffprobe 素材指纹
#   env.txt        机型/系统/构建开关
#   verdict.txt    完整性校验结论
set -uo pipefail

# 素材名以 http:// 开头即网络源(net-playback-harness 步骤4)。
# 判定放在这里而不是加 --url 开关: 用例定义里只有一个"源"的概念,
# 多一个开关就多一处可能与素材对不上的地方
ASSET="${1:?素材名，或 http://127.0.0.1:8188/xxx.mp4}"
SOFTWARE="${2:-true}"
PLAY_SECONDS="${3:-15}"
SEEK_PERCENTS="${4:-}"
CASE="${5:-$(basename "$ASSET" .mp4)-$([ "$SOFTWARE" = true ] && echo sw || echo hw)}"

PKG=com.example.lzplayer
ACT=$PKG/.console.ConsoleActivity
DEV_DIR=/sdcard/Movies
APP_FILES=/sdcard/Android/data/$PKG/files
OUT="test-reports/raw/$CASE"
mkdir -p "$OUT"

# 本机素材路径（gen-test-assets.sh 的默认输出目录），用于 ffprobe 指纹。
# 指纹取自本机副本而不是设备：ffprobe 在宿主机现成，且两者是同一份文件
LOCAL_ASSET="/tmp/lzplayer-assets/$ASSET"
[ "${ASSET#http}" != "$ASSET" ] && LOCAL_ASSET="assets/serving/$(basename "$ASSET")"

echo "== 用例 $CASE =="
echo "   素材=$ASSET 软解=$SOFTWARE 稳态=${PLAY_SECONDS}s seek=${SEEK_PERCENTS:-无}"

# —— 环境指纹。报告缺任何一项就该拒绝出报告(7d 验收), 所以这里必须全采到 ——
{
  echo "case=$CASE"
  echo "asset=$ASSET"
  echo "software=$SOFTWARE"
  echo "playSeconds=$PLAY_SECONDS"
  echo "seekPercents=${SEEK_PERCENTS:-}"
  echo "device=$(adb shell getprop ro.product.model | tr -d '\r')"
  echo "os=$(adb shell getprop ro.build.version.release | tr -d '\r')"
  echo "build=$(adb shell getprop ro.build.display.id | tr -d '\r')"
  # 构建开关直接影响读数: VE_QUIET_LOG 会编掉 V/D/I, VE_TRACE_FRAME 会让每帧
  # 日志打爆配额。harness 无法从设备读到它们, 所以记录"未知"而不是假装知道
  echo "veQuietLog=unknown(见构建命令)"
  echo "veTraceFrame=unknown(见构建命令)"
} > "$OUT/env.txt"

# —— 素材指纹 ——
if [ -f "$LOCAL_ASSET" ]; then
  ffprobe -v error -show_entries \
    stream=codec_type,codec_name,width,height,r_frame_rate,duration,bit_rate \
    -of json "$LOCAL_ASSET" > "$OUT/asset.json" 2>/dev/null
  # 关键帧位置单独存: seek 精度判据要对它, 而这是唯一可信来源
  # (本项目曾三次因"按编码参数推算关键帧位置"而下错结论)
  ffprobe -v error -select_streams v:0 -skip_frame nokey \
    -show_entries frame=pts_time -of csv=p=0 "$LOCAL_ASSET" 2>/dev/null \
    | tr -d ',' > "$OUT/keyframes.txt"
else
  echo "{}" > "$OUT/asset.json"
  echo "!! 本机找不到 $LOCAL_ASSET，素材指纹缺失（先跑 gen-test-assets.sh）"
fi

# —— 采集 ——
adb shell "rm -f $APP_FILES/$CASE-snapshot.json" >/dev/null 2>&1
adb logcat -c
adb shell am force-stop $PKG >/dev/null 2>&1
case "$ASSET" in
  http://*|https://*)
      SRC_URI="$ASSET"
      # 网络源: 服务器请求日志并入产物, 它是判据的独立交叉源
      NET=1 ;;
  *)  SRC_URI="$DEV_DIR/$ASSET"; NET=0 ;;
esac
CMD="am start -n $ACT -e source $SRC_URI --ez autoplay true --ez software $SOFTWARE"
CMD="$CMD --ei playSeconds $PLAY_SECONDS --es caseName $CASE"
[ -n "$SEEK_PERCENTS" ] && CMD="$CMD --es seekPercents $SEEK_PERCENTS"
# 整条命令加引号在设备侧 shell 执行: URL query 里的 & 否则会被当成后台
# 运行符吃掉, 注入参数静默丢失 —— 实测 ?kbps=2000&stall=6@0.25 只剩
# kbps=2000, 断流场景连着三次"什么都没测到"却不报错
adb shell "$CMD" >/dev/null 2>&1
echo "$CMD" >> "$OUT/env.txt"

# 等到快照出现或超时。收尾时刻 = 稳态 + 每个 seek 3s(见 runSeekStep) + 余量
SEEK_N=$(echo "${SEEK_PERCENTS:-}" | awk -F, '{print NF}')
[ -z "$SEEK_PERCENTS" ] && SEEK_N=0
DEADLINE=$(( PLAY_SECONDS + SEEK_N * 3 + 15 ))
for _ in $(seq 1 "$DEADLINE"); do
  sleep 1
  adb shell "test -f $APP_FILES/$CASE-snapshot.json" >/dev/null 2>&1 && break
done

# 只取收尾快照之前的时间线: 序列收尾后 app 并未退出, 播放还在继续,
# 那之后的 VESTAT 不属于本用例的采样窗口。混进来会让缺号检测误报 ——
# 实测 net-53min 收尾于 t=25, 而 t=42 又冒出一条, 中间 17 秒被判缺号。
adb logcat -d 2>/dev/null | grep -aE "VESTAT|VERENDER|VEGAUGE|VEBENCH" \
  | awk -v n="$PLAY_SECONDS" '
      /VESTAT t=/ { if (match($0, /t=[0-9]+/)) {
                      t = substr($0, RSTART+2, RLENGTH-2) + 0
                      if (t > n) next } }
      { print }' > "$OUT/timeline.txt" || true
adb shell "cat $APP_FILES/$CASE-snapshot.json" > "$OUT/snapshot.json" 2>/dev/null || true

# —— 完整性校验。**这一步的结论比数字本身重要**：时间线缺号就不能用于对照，
#    而缺号在聚合值里完全看不出来 ——
python3 - "$OUT" <<'PY'
import io, os, re, sys, json
out = sys.argv[1]
v = []
lines = io.open(os.path.join(out, "timeline.txt"), errors="ignore").read().splitlines()
secs = sorted(int(m.group(1)) for m in
              (re.search(r"VESTAT t=(\d+)", L) for L in lines) if m)
if not secs:
    v.append("FAIL 时间线为空 —— 没有任何 VESTAT 行")
else:
    missing = [s for s in range(1, secs[-1] + 1) if s not in set(secs)]
    if missing:
        # 逐秒时间线缺号意味着日志被配额丢过, 这一份不能用于跨轮次对照
        v.append("FAIL 时间线缺号 %d 个: %s" % (len(missing), missing[:20]))
    else:
        v.append("PASS 时间线连续 t=1..%d" % secs[-1])

snap = os.path.join(out, "snapshot.json")
if not os.path.exists(snap) or os.path.getsize(snap) == 0:
    v.append("FAIL 快照缺失 —— 序列未收尾(app 被杀 / playSeconds 未到)")
else:
    try:
        d = json.load(io.open(snap))
        v.append("PASS 快照可解析 complete=%s" % d.get("complete"))
        if not d.get("complete"):
            v.append("FAIL complete=false —— 中途被打断, 数据不完整")
        for k in ("startup", "stats"):
            if not d.get(k):
                v.append("FAIL 快照缺 %s 段" % k)
        st = d.get("seekTrace") or {}
        v.append("INFO seekTrace.count=%s" % st.get("count"))
    except Exception as e:
        v.append("FAIL 快照 JSON 解析失败: %s" % e)

rej = [L for L in lines if "VEBENCH REJECT" in L]
if rej:
    # 被拒绝的参数必须出现在结论里: 否则"参数写错导致一次 seek 都没做"
    # 会被当成"seek 全部通过"
    v.append("FAIL 有被拒绝的跑分参数 %d 条" % len(rej))
    v += ["     " + L.split("VEBENCH")[-1].strip() for L in rej[:5]]

io.open(os.path.join(out, "verdict.txt"), "w").write("\n".join(v) + "\n")
print("\n".join(v))
PY

# 网络源: 服务器字节级日志是判据的独立交叉源, 必须随产物归档
[ "${NET:-0}" = "1" ] && [ -f assets/server-requests.log ] && \
  cp assets/server-requests.log "$OUT/server-requests.log"
echo "== 产物: $OUT =="
