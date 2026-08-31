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
# 素材可能以设备绝对路径给出(/sdcard/Movies/xxx.mp4), 直接拼前缀会得到
# assets/generated//sdcard/Movies/xxx.mp4 —— 文件找不到, keyframes.txt 静默
# 为空, seek 精度判据于是无声地失去交叉校验来源。一律先取 basename。
LOCAL_ASSET="assets/generated/$(basename "$ASSET")"
# 取本机副本路径时必须**先去掉 URL query**: 注入场景的 URL 形如
# xxx.mp4?kbps=1440&stall=6@0.25, basename 会把整串 query 带进文件名,
# 找不到文件 → 素材指纹为空 → 后续全部失败。实测 12 个场景里 6 个带注入的
# 全军覆没, 且只剩 asset.json(3 字节)与 env.txt, 症状不明显
if [ "${ASSET#http}" != "$ASSET" ]; then
    LOCAL_ASSET="assets/serving/$(basename "${ASSET%%\?*}")"
fi

echo "== 用例 $CASE =="
echo "   素材=$ASSET 软解=$SOFTWARE 稳态=${PLAY_SECONDS}s seek=${SEEK_PERCENTS:-无}" \
     "变速=${SPEEDS:-无} 切轨=${TRACKS:-无}"

# —— 环境指纹。报告缺任何一项就该拒绝出报告(7d 验收), 所以这里必须全采到 ——
{
  echo "case=$CASE"
  echo "asset=$ASSET"
  echo "software=$SOFTWARE"
  echo "playSeconds=$PLAY_SECONDS"
  echo "seekPercents=${SEEK_PERCENTS:-}"
  echo "expect=${EXPECT:-}"
  echo "speeds=${SPEEDS:-}"
  echo "tracks=${TRACKS:-}"
  echo "device=$(adb shell getprop ro.product.model | tr -d '\r')"
  echo "os=$(adb shell getprop ro.build.version.release | tr -d '\r')"
  echo "build=$(adb shell getprop ro.build.display.id | tr -d '\r')"
  # 构建开关直接影响读数: VE_QUIET_LOG 会编掉 V/D/I, VE_TRACE_FRAME 会让每帧
  # 日志打爆配额。harness 无法从设备读到它们, 所以记录"未知"而不是假装知道
  echo "veQuietLog=unknown(见构建命令)"
  echo "veTraceFrame=unknown(见构建命令)"
  # **注入状态必须进指纹**: 一次忘关的注入会让后续所有结论作废且极难发现
  echo "faultHwCreate=${FAULT_HW_CREATE:-}"
  echo "faultHwConfigure=${FAULT_HW_CONFIGURE:-}"
  echo "faultHwAfterFrames=${FAULT_HW_AFTER:-}"
} > "$OUT/env.txt"

# generated 之外还可能落在 serving/ 或 format/。**回退查找而不是要求调用方
# 记住素材在哪**: 记错的代价是判据静默失去交叉校验来源, 而不是一个响亮的报错
if [ ! -f "$LOCAL_ASSET" ]; then
    for d in assets/serving assets/format assets; do
        if [ -f "$d/$(basename "${ASSET%%\?*}")" ]; then
            LOCAL_ASSET="$d/$(basename "${ASSET%%\?*}")"
            break
        fi
    done
fi

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
  # 变量名必须加花括号: 后面紧跟中文全角逗号时, shell 会把逗号当成变量名的
  # 一部分, set -u 下报 unbound variable。这条一直存在, 只是素材总能找到、
  # 从没走进这个 else 分支
  echo "!! 本机找不到 ${LOCAL_ASSET}，素材指纹缺失（先跑 gen-test-assets.sh）"
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
  /*) # 绝对路径原样用, 不再拼 DEV_DIR —— 否则得到
      # /sdcard/Movies//sdcard/Movies/xxx.mp4, 播放起不来而报告只显示
      # "解码路径 FAIL", 症状与播放器缺陷难以区分
      SRC_URI="$ASSET"; NET=0 ;;
  *)  SRC_URI="$DEV_DIR/$ASSET"; NET=0 ;;
esac
# **source 必须单引号包起来。**
# 宿主机侧给 adb shell 加引号是不够的: adb 把 argv 拼成一个字符串交给
# **设备侧 shell 重新解析**, URL query 里的 & 在那里仍是裸的后台运行符。
# 后果不只是丢注入参数, 而是 & 之后的**所有 am 参数一起丢**(autoplay /
# caseName / playSeconds), 于是不起播、快照没名字, 报告九条全 INCONCLUSIVE。
# 实测 ?kbps=2000&stall=6@0.25 到服务端只剩 kbps=2000, 状态机停在 8(PREPARED)。
# 单引号让设备侧 shell 把整个 URL 当字面量。
CMD="am start -n $ACT -e source '$SRC_URI' --ez autoplay true --ez software $SOFTWARE"
# 故障注入经环境变量透传(仅 -PveFaultInject=true 构建有效)。
# 走环境变量而非位置参数: 注入是可选的第 6~8 个维度, 加成位置参数会让
# 常规调用也得写一串空串
[ -n "${FAULT_HW_CREATE:-}" ]    && CMD="$CMD --ez faultHwCreate $FAULT_HW_CREATE"
[ -n "${FAULT_HW_CONFIGURE:-}" ] && CMD="$CMD --ez faultHwConfigure $FAULT_HW_CONFIGURE"
[ -n "${FAULT_HW_AFTER:-}" ]     && CMD="$CMD --ei faultHwAfterFrames $FAULT_HW_AFTER"
CMD="$CMD --ei playSeconds $PLAY_SECONDS --es caseName $CASE"
[ -n "$SEEK_PERCENTS" ] && CMD="$CMD --es seekPercents $SEEK_PERCENTS"
# 变速/切轨序列(perf-metrics 9f)。经环境变量而不是位置参数传入 —— 位置参数
# 已经四个了，再加会让所有现存调用点都得数逗号。
[ -n "${SPEEDS:-}" ] && CMD="$CMD --es speeds $SPEEDS"
[ -n "${TRACKS:-}" ] && CMD="$CMD --es tracks $TRACKS"
# (URL query 里 & 的处理见上面 CMD 构造处的注释: 靠的是给 source 加**单引号**,
#  不是给整条命令加引号 —— 后者不起作用, 这条曾被误记为已解决)
# 日志落**设备本地**再一次性拉取, 不用宿主机流式 adb logcat。
#
# 两个都要避开的坑:
# 1. 跑完再 adb logcat -d 会被环形缓冲截断 —— 实测 5 分钟长稳只留最后
#    103 秒, 前 195 秒全丢;
# 2. 宿主机流式 `adb logcat > file &` 会与 adb reverse **抢同一条 USB
#    通道**, 把网络带宽吃掉 —— 实测同素材同参数, 有流式采集时 fps 均值
#    1.4(最低 0), 无流式时 23.9。**测量工具反过来污染了被测对象**,
#    且伪装得很好: 数据完整、内存斜率也正常, 只有 fps 露馅。
# 先杀残留采集进程并删旧文件, 否则多次运行的日志会叠在同一文件里 ——
# 实测出现过"样本 415 条却只覆盖 297 秒"、fps 均值被上一轮数据拉偏
# 每次运行用**独立文件名**, 而不是复用同一个再删。
# 实测复用时 rm 不生效: 采集进程仍持有文件句柄, 新一轮日志追加在旧内容之后,
# 于是"样本 712 条却只有 297 个唯一秒号"、三段长度 [118,297,297] ——
# 第一段 118 正是上一轮的长度。据此算出的泄漏斜率与 fps 全部不可信。
DEV_LOG="/sdcard/bench-$CASE.txt"
adb shell "pkill -f 'logcat -f /sdcard/bench-'" >/dev/null 2>&1
sleep 1
adb shell "rm -f $DEV_LOG; logcat -c; nohup logcat -f $DEV_LOG > /dev/null 2>&1 &" >/dev/null 2>&1
adb shell "$CMD" >/dev/null 2>&1
echo "$CMD" >> "$OUT/env.txt"

# 等到快照出现或超时。收尾时刻 = 稳态 + 每个 seek 3s(见 runSeekStep) + 余量
SEEK_N=$(echo "${SEEK_PERCENTS:-}" | awk -F, '{print NF}')
[ -z "$SEEK_PERCENTS" ] && SEEK_N=0
# 变速与切轨序列排在 seek 之后, 各自也按 3 秒一步推进 —— 不把它们算进
# 截止时间的话, 序列还没走完 harness 就开始收尾, 快照里会少掉后半段样本
SPEED_N=$(echo "${SPEEDS:-}" | awk -F, '{print NF}')
[ -z "${SPEEDS:-}" ] && SPEED_N=0
# 变速序列跑完还会多一步"还原 1.0"(见 ConsoleActivity.runSpeedStep), 也占 3 秒
[ "$SPEED_N" -gt 0 ] && SPEED_N=$(( SPEED_N + 1 ))
TRACK_N=$(echo "${TRACKS:-}" | awk -F, '{print NF}')
[ -z "${TRACKS:-}" ] && TRACK_N=0
DEADLINE=$(( PLAY_SECONDS + (SEEK_N + SPEED_N + TRACK_N) * 3 + 15 ))
for _ in $(seq 1 "$DEADLINE"); do
  sleep 1
  adb shell "test -f $APP_FILES/$CASE-snapshot.json" >/dev/null 2>&1 && break
done

# 只取收尾快照之前的时间线: 序列收尾后 app 并未退出, 播放还在继续,
# 那之后的 VESTAT 不属于本用例的采样窗口。混进来会让缺号检测误报 ——
# 实测 net-53min 收尾于 t=25, 而 t=42 又冒出一条, 中间 17 秒被判缺号。
#
# **截断点是"序列结束"而不是 PLAY_SECONDS。** 后者只是稳态段, seek/变速/
# 切轨三个序列全排在它之后(各 3 秒一步), 拿它当上界会把整个序列期的样本
# 全部丢掉 —— 报告里 post_seek 段因此**恒为空**, seek 之后的任何判据都
# 无从做起(实测 playSeconds=12 的用例日志有 t=17, timeline.txt 只到 t=12)。
# 末尾再留 3 秒: post_seek 观察窗要看最后一次 seek 之后那几秒。
SEQ_END=$(( PLAY_SECONDS + (SEEK_N + SPEED_N + TRACK_N) * 3 + 3 ))
adb shell "pkill -f 'logcat -f /sdcard/bench-'" >/dev/null 2>&1
sleep 1
adb pull "$DEV_LOG" "$OUT/logcat-stream.txt" >/dev/null 2>&1
adb shell "rm -f $DEV_LOG" >/dev/null 2>&1
cat "$OUT/logcat-stream.txt" 2>/dev/null | grep -aE "VESTAT|VERENDER|VEGAUGE|VEBENCH" \
  | awk -v n="$SEQ_END" '
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
