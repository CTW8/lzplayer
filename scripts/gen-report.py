#!/usr/bin/env python3
"""跑分报告生成器（perf-metrics 步骤7d + 7e）。

读 run-benchmark.sh 落下的原始包，产出 report.txt 与 report.json。
**两者由同一中间结构渲染**——否则两份数字会各自漂移，而读报告的人无从判断
哪份可信。

7e 的统计口径写死在这里，不交给读报告的人：
  1. 所有逐秒列**整段统计**，给 n/mean/p50/p95/max，禁止只报某一行。
     CPU 列单次运行内方差达 30 个百分点，挑几行就能得出相反结论。
  2. 统计窗口**显式分段**：起播段 / 稳态段 / 每次 seek 后 2 秒各自成段，
     不许混算——seek 后的追帧会污染稳态读数。
  3. 每段给 n，n < 30 的分位数发 "--"（同 VEPerfHistogram::kMinSamples）。
  4. 指纹缺任何一项 → 拒绝出报告，而不是留空。

用法: ./scripts/gen-report.py test-reports/raw/<用例>
"""
import io
import json
import os
import re
import sys

# 样本不足时不给分位数。与 native 侧 VEPerfHistogram::kMinSamples 一致——
# 两边阈值不同会让同一份数据在面板和报告里一个有数一个没数
MIN_SAMPLES = 30

# 每一列的采样时刻说明。**必须进报告图例**：本项目栽过的坑里有一半是
# "同一个函数在不同时刻调用，一个有意义一个纯误导"（avOffMs 就是因此被删）
COLUMN_NOTES = {
    "fps": "本秒实际上屏帧数（presentInterval 样本数差值）",
    "dropLate": "本秒新增，迟到丢帧。阈值 100ms，即落后 3 帧才丢",
    "dropOvf": "本秒新增，credit 记账失守。出现即 bug",
    "dropStale": "本秒新增，flush/seek 后的在途旧帧。正常",
    "dropSeek": "本秒新增，精准 seek 追帧丢弃。属 seek 成本，非缺陷",
    "vpark": "本秒新增，视频解码器 credit 用尽次数。判断上下游瓶颈的关键",
    "apark": "本秒新增，音频解码器 credit 用尽次数",
    "vstarve": "本秒新增，视频上游饥饿次数",
    "astarve": "本秒新增，音频上游饥饿次数",
    "aq": "瞬时深度（非峰值）。-1 = 该轨不存在",
    "vq": "瞬时深度（非峰值）。-1 = 该轨不存在",
    "fq": "瞬时深度。-1 = 硬解不走 VEVideoDisplay",
    "syncWorstMs": "本秒最差同步余量，在 renderFrame 之前采样。"
                   "负=已迟到；-9999 = 本秒无帧上屏（哨兵，非 0）",
    "cpu": "进程 CPU，单核归一（100 = 吃满一核）。-9999 = 未采到",
}


def pct(vals, q):
    """分位数。样本不足返回 None，由渲染层显示 '--'——
    造一个数出来会被当成真实读数。"""
    if len(vals) < MIN_SAMPLES:
        return None
    s = sorted(vals)
    return s[min(int(len(s) * q), len(s) - 1)]


def summarize(vals):
    """整段统计。哨兵值(-9999)在统计前剔除：它是'没测到'，
    混进均值会把结果拉成负数而看起来像个真实读数。"""
    clean = [v for v in vals if v > -9000]
    if not clean:
        return {"n": 0}
    return {
        "n": len(clean),
        "mean": round(sum(clean) / len(clean), 2),
        "p50": pct(clean, 0.50),
        "p95": pct(clean, 0.95),
        "max": round(max(clean), 2),
        "dropped_sentinels": len(vals) - len(clean),
    }


def parse_timeline(path):
    rows = []
    for line in io.open(path, errors="ignore"):
        m = re.search(r"VESTAT (t=\d+ .*)", line)
        if not m:
            continue
        kv = {}
        for tok in m.group(1).split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                try:
                    kv[k] = float(v)
                except ValueError:
                    kv[k] = v
        rows.append(kv)
    return rows


def segment(rows, seek_secs, play_seconds):
    """按窗口切段。起播段取前 2 秒（首帧到稳态之间读数必然异常，
    混进稳态会污染），seek 后 2 秒各自成段，其余是稳态。"""
    segs = {"startup": [], "steady": [], "post_seek": []}
    seek_windows = set()
    for s in seek_secs:
        seek_windows |= {s, s + 1, s + 2}
    for r in rows:
        t = int(r.get("t", 0))
        if t <= 2:
            segs["startup"].append(r)
        elif t in seek_windows:
            segs["post_seek"].append(r)
        else:
            segs["steady"].append(r)
    return segs


def build(raw_dir):
    env = {}
    p = os.path.join(raw_dir, "env.txt")
    if os.path.exists(p):
        for line in io.open(p, errors="ignore"):
            if "=" in line:
                k, v = line.strip().split("=", 1)
                env[k] = v

    # 指纹缺项即拒绝出报告(7d 验收)。留空的报告会被当成"这项不适用",
    # 而实际是"没采到"——两者结论完全不同
    required = ["case", "asset", "software", "device", "os", "build"]
    missing_env = [k for k in required if not env.get(k)]

    asset = {}
    ap = os.path.join(raw_dir, "asset.json")
    if os.path.exists(ap):
        try:
            asset = json.load(io.open(ap))
        except Exception:
            asset = {}
    if not asset.get("streams"):
        missing_env.append("asset.json(素材指纹)")

    snap = {}
    sp = os.path.join(raw_dir, "snapshot.json")
    if os.path.exists(sp) and os.path.getsize(sp) > 0:
        try:
            snap = json.load(io.open(sp))
        except Exception:
            snap = {}

    rows = parse_timeline(os.path.join(raw_dir, "timeline.txt"))

    play = int(float(env.get("playSeconds", 0) or 0))
    n_seek = len([x for x in (env.get("seekPercents") or "").split(",") if x.strip()])
    # seek 发生在哪一秒: **实测**, 拿 VEBENCH seek 行的 logcat 时刻减去第一条
    # VESTAT 的时刻。
    #
    # 原先按"稳态时长 + 3s 步距"推算, 第一次运行就错了: playSeconds 一旦大于
    # 片长, 播放早已 EOS, seek 并不在推算的那些秒发生(实测推出 [21,24], 而
    # 时间线只到 10)。凡"由参数推算实际发生时刻"的做法都有这个毛病 ——
    # 参数是意图, 日志才是事实。
    seek_secs = []
    tl_path = os.path.join(raw_dir, "timeline.txt")
    if os.path.exists(tl_path):
        lines = io.open(tl_path, errors="ignore").read().splitlines()
        def stamp(line):
            m = re.match(r"\d[\d-]* (\d\d):(\d\d):(\d\d\.\d+)", line)
            if not m:
                return None
            return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
        t0 = next((stamp(l) for l in lines if "VESTAT" in l and stamp(l)), None)
        if t0 is not None:
            for l in lines:
                if "VEBENCH seek" in l:
                    ts = stamp(l)
                    if ts is not None:
                        # VESTAT t=1 对应 t0, 故秒号 = 差值 + 1
                        seek_secs.append(int(ts - t0) + 1)

    segs = segment(rows, seek_secs, play)
    stats = {}
    for name, rr in segs.items():
        stats[name] = {}
        for col in COLUMN_NOTES:
            vals = [r[col] for r in rr if isinstance(r.get(col), float)]
            if vals:
                stats[name][col] = summarize(vals)

    # —— 判据。缺交叉校验来源的一律 INCONCLUSIVE, 不许算 PASS ——
    st = snap.get("stats") or {}
    startup = snap.get("startup") or {}
    seek = snap.get("seekTrace") or {}
    hw = env.get("software") == "false"
    crit = []

    def add(name, verdict, measured, cross):
        crit.append({"criterion": name, "verdict": verdict,
                     "measured": measured, "cross_check": cross})

    dp = st.get("decodePath") or startup.get("decodePath")
    if dp is None:
        add("解码路径符合预期", "INCONCLUSIVE", "decodePath 缺失", "—")
    else:
        want = "hardware" if hw else "software"
        add("解码路径符合预期", "PASS" if dp == want else "FAIL",
            "decodePath=%s" % dp, "intent software=%s" % env.get("software"))

    steady_fps = stats.get("steady", {}).get("fps", {})
    fr = None
    for s in asset.get("streams", []):
        if s.get("codec_type") == "video" and s.get("r_frame_rate"):
            try:
                a, b = s["r_frame_rate"].split("/")
                fr = float(a) / float(b)
            except Exception:
                pass
    if not steady_fps.get("n") or fr is None:
        add("稳态帧率达标", "INCONCLUSIVE",
            "n=%s" % steady_fps.get("n"), "素材帧率=%s" % fr)
    else:
        ok = steady_fps.get("p50") is not None and steady_fps["p50"] >= fr * 0.95
        add("稳态帧率达标", "PASS" if ok else ("INCONCLUSIVE"
            if steady_fps.get("p50") is None else "FAIL"),
            "稳态 p50=%s (n=%d)" % (steady_fps.get("p50"), steady_fps["n"]),
            "素材 r_frame_rate=%.2f" % fr)

    ovf = stats.get("steady", {}).get("dropOvf", {})
    if not ovf.get("n"):
        add("无 credit 记账失守", "INCONCLUSIVE", "dropOvf 无样本", "—")
    else:
        add("无 credit 记账失守", "PASS" if ovf.get("max") == 0 else "FAIL",
            "稳态 dropOvf max=%s" % ovf.get("max"),
            "vpark/apark 有增长即流控在工作")

    if not seek.get("count"):
        add("seek 追踪有记录", "INCONCLUSIVE",
            "seekTrace.count=%s" % seek.get("count"),
            "发起 %d 次" % n_seek)
    else:
        add("seek 追踪有记录", "PASS" if seek["count"] >= min(n_seek, 10) else "FAIL",
            "count=%d" % seek["count"], "发起 %d 次(环形上限 10)" % n_seek)

    return {
        "env": env, "missing_fingerprint": missing_env,
        "asset": asset, "startup": startup, "seekTrace": seek,
        "segments": {k: len(v) for k, v in segs.items()},
        "seek_seconds_measured": seek_secs,
        "stats": stats, "criteria": crit,
        "timeline": rows, "column_notes": COLUMN_NOTES,
    }


def render(rep):
    L = []
    A = L.append
    A("=" * 74)
    A("跑分报告  用例 %s" % rep["env"].get("case", "?"))
    A("=" * 74)
    if rep["missing_fingerprint"]:
        A("")
        A("!! 拒绝出报告：环境指纹缺项 —— %s" % ", ".join(rep["missing_fingerprint"]))
        A("   留空的报告会被当成'这项不适用'，而实际是'没采到'。")
        return "\n".join(L) + "\n"

    A("")
    A("[1] 环境指纹")
    for k in ("device", "os", "build", "asset", "software", "playSeconds",
              "seekPercents", "veQuietLog", "veTraceFrame"):
        A("    %-14s %s" % (k, rep["env"].get(k, "—")))
    for s in rep["asset"].get("streams", []):
        A("    素材流         %s %s %sx%s %s %ss" % (
            s.get("codec_type"), s.get("codec_name"), s.get("width", "-"),
            s.get("height", "-"), s.get("r_frame_rate", "-"), s.get("duration")))

    A("")
    A("[2] 判据（缺交叉校验来源的标 INCONCLUSIVE，不计入通过）")
    for c in rep["criteria"]:
        A("    %-12s %s" % (c["verdict"], c["criterion"]))
        A("                 实测: %s" % c["measured"])
        A("                 交叉: %s" % c["cross_check"])

    A("")
    A("[3] 分段统计（窗口不混算：seek 后追帧会污染稳态）")
    A("    段落样本数: %s" % rep["segments"])
    A("    seek 所在秒(实测自 VEBENCH 日志时刻): %s"
      % (rep["seek_seconds_measured"] or "无 —— seek 未发生或日志已滚掉"))
    for seg in ("startup", "steady", "post_seek"):
        s = rep["stats"].get(seg) or {}
        if not s:
            continue
        A("")
        A("    -- %s --" % seg)
        A("    %-12s %5s %9s %9s %9s %9s" % ("列", "n", "mean", "p50", "p95", "max"))
        for col, v in s.items():
            if not v.get("n"):
                continue
            f = lambda x: "--" if x is None else ("%.1f" % x)
            A("    %-12s %5d %9s %9s %9s %9s" % (
                col, v["n"], f(v.get("mean")), f(v.get("p50")),
                f(v.get("p95")), f(v.get("max"))))

    A("")
    A("[4] 列的采样时刻（同一个量在不同时刻采样，含义可能完全不同）")
    for k, v in rep["column_notes"].items():
        A("    %-12s %s" % (k, v))

    A("")
    A("[5] 逐秒时间线（%d 行）" % len(rep["timeline"]))
    cols = ["t", "fps", "dropLate", "vpark", "apark", "aq", "vq", "fq",
            "syncWorstMs", "cpu"]
    A("    " + " ".join("%8s" % c for c in cols))
    for r in rep["timeline"]:
        A("    " + " ".join(
            "%8s" % (("%.1f" % r[c]) if isinstance(r.get(c), float) else "-")
            for c in cols))
    A("")
    A("p50/p95 为 '--' 表示样本数 < %d，不是 0。" % MIN_SAMPLES)
    return "\n".join(L) + "\n"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    d = sys.argv[1]
    rep = build(d)
    io.open(os.path.join(d, "report.txt"), "w").write(render(rep))
    io.open(os.path.join(d, "report.json"), "w").write(
        json.dumps(rep, ensure_ascii=False, indent=2))
    print(render(rep))
    return 0


if __name__ == "__main__":
    sys.exit(main())
