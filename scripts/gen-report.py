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
    "rssMb": "进程驻留内存。**判泄漏看斜率不看绝对值**——绝对值受素材分辨率"
             "与缓存配置影响很大（32MB 环形缓存 + 解码缓冲即占大头）",
    "fd": "打开的文件描述符数。与 RSS 是两种独立故障，只看 RSS 会漏掉 fd 泄漏",
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
        # 期望失败的场景(坏内容)本来就 probe 不出流信息 —— 那正是它要验的东西。
        # 一律拒绝出报告的话，唯一能验证错误链路的用例反而永远出不了报告。
        if env.get("expect") == "error":
            asset = {"streams": [], "probe_failed": True}
        else:
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

    # 有些用例**期望失败**(坏内容、永久断流)。对它们套"解码路径符合预期"
    # "稳态帧率达标"这类正常播放判据，FAIL 才是对的结果 —— 判据被套错场景
    # 已经在 throttle-below 上栽过一次(见下面限速那段)，这里不再重演。
    # 期望由用例显式声明(EXPECT=error)，不靠用例名猜。
    expect_error = env.get("expect") == "error"
    if expect_error:
        # 唯一该判的是错误链路本身：既要有 ON_ERROR 上报，终态也要是
        # STATE_ERROR。**两个都要**：只看状态码会漏掉"状态对了但上层没收到
        # 通知"，只看通知会漏掉"报了错却停在别的状态"。
        #
        # 状态码取自 VEPlayerDriver 的枚举，注意 STATE_ERROR=0、IDLE=1、
        # PLAYBACK_COMPLETE=128 —— 这三个此前被读反过，把
        # "STARTED -> STATE_ERROR" 记成了"静默退回 IDLE"，又把
        # "STARTED -> PLAYBACK_COMPLETE" 记成了"进了错误态"。
        lg2 = os.path.join(raw_dir, "logcat-stream.txt")
        n_err, last_state = 0, None
        if os.path.exists(lg2):
            t2 = io.open(lg2, errors="ignore").read()
            n_err = t2.count("VE_PLAYER_NOTIFY_EVENT_ON_ERROR")
            tr = re.findall(r"state (\d+) -> (\d+)", t2)
            if tr:
                last_state = int(tr[-1][1])
        ok_err = n_err >= 1 and last_state == 0
        add("错误链路穿过（期望失败的场景）",
            "PASS" if ok_err else "FAIL",
            "ON_ERROR=%d 终态=%s" % (n_err, last_state),
            "期望 ON_ERROR>=1 且终态=0(STATE_ERROR)；"
            "IDLE 是 1、PLAYBACK_COMPLETE 是 128，别读混")
        add("解码路径符合预期", "INCONCLUSIVE",
            "本场景期望失败，正常播放判据不适用", "EXPECT=error")

    dp = st.get("decodePath") or startup.get("decodePath")
    if expect_error:
        pass
    elif dp is None:
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
    # 限速注入的取值：写在素材 URL 的 query 里(kbps=1440)，是本用例期望值的
    # 一部分。**不能对所有场景套同一条"帧率达标"判据** —— throttle-below 的
    # 全部意义就是帧率应该塌下去，拿达标去判它，PASS 才是错的。
    # 08-22 那次 FAIL 一直靠人工覆盖，后来样本数掉到门槛下变成 INCONCLUSIVE
    # 把问题藏了起来，就是这条缺陷的代价。
    asset_url = env.get("asset", "")
    kbps = None
    m_kbps = re.search(r"[?&]kbps=(\d+)", asset_url)
    if m_kbps:
        kbps = int(m_kbps.group(1))
    total_bps = 0
    # 循环变量**不能叫 st** —— 外层的 st 是 stats 字典，被遮蔽后后面所有
    # 读 st 的判据都会拿到"最后一条流"，实测把 seek 记账判据静默变成了
    # INCONCLUSIVE(数据其实齐全)。判据工具自己的哑火与被测代码的 bug 同等重要。
    for _stream in asset.get("streams", []):
        try:
            total_bps += int(_stream.get("bit_rate") or 0)
        except (TypeError, ValueError):
            pass
    # 限速是否在瓶颈之下：留 5% 余量，贴着码率的限速两边都说不准
    throttled_below = (kbps is not None and total_bps > 0
                       and kbps * 1000 < total_bps * 0.95)

    if expect_error:
        add("稳态帧率达标", "INCONCLUSIVE",
            "本场景期望失败，不该有稳态", "EXPECT=error")
    elif not steady_fps.get("n") or fr is None:
        add("稳态帧率达标", "INCONCLUSIVE",
            "n=%s" % steady_fps.get("n"), "素材帧率=%s" % fr)
    elif steady_fps.get("p50") is None:
        # 判据名要跟着场景走，否则汇总表上"稳态帧率达标 INCONCLUSIVE"会被读成
        # "帧率没测出来"，而 throttle-below 根本不该用这条判据
        add("限速降级符合预期（帧率应下降）" if throttled_below else "稳态帧率达标",
            "INCONCLUSIVE",
            "稳态 p50=None (n=%d < 30，分位数不成立)" % steady_fps["n"],
            "素材 r_frame_rate=%.2f" % fr)
    elif throttled_below:
        # 期望反过来：限速到瓶颈之下时帧率**必须**下降。仍然满帧说明限速
        # 根本没生效 —— 那这个用例什么都没测到，比 FAIL 更该被拦下来
        degraded = steady_fps["p50"] < fr * 0.9
        add("限速降级符合预期（帧率应下降）", "PASS" if degraded else "FAIL",
            "稳态 p50=%.1f (n=%d)" % (steady_fps["p50"], steady_fps["n"]),
            "限速 %dkbps < 素材码率 %.0fkbps；仍满帧则是注入未生效"
            % (kbps, total_bps / 1000.0))
    else:
        ok = steady_fps["p50"] >= fr * 0.95
        add("稳态帧率达标", "PASS" if ok else "FAIL",
            "稳态 p50=%s (n=%d)" % (steady_fps.get("p50"), steady_fps["n"]),
            "素材 r_frame_rate=%.2f%s" % (
                fr, "；限速 %dkbps 在码率之上，不应降级" % kbps if kbps else ""))

    # ---- buffering 事件成对 ----
    #
    # 网络场景唯一能直接判"卡顿处理是否自洽"的东西：每次 START 都该有对应的
    # END。只多一个 END 或只多一个 START 都是 bug —— 本项目修过的五个网络
    # bug 里就有两个是这个形状(事件乱序、恢复信号来自被自己暂停的路径)。
    # 允许差 1：用例收尾时最后一次 buffering 可能还在途中。
    lg = os.path.join(raw_dir, "logcat-stream.txt")
    n_start = n_end = 0
    if os.path.exists(lg):
        txt = io.open(lg, errors="ignore").read()
        n_start = txt.count("buffering, pausing data flow")
        n_end = txt.count("buffering done, resuming")
    if n_start == 0 and n_end == 0:
        add("buffering 事件成对", "INCONCLUSIVE",
            "本用例未发生 buffering", "本地素材或带宽充足时没有样本，不是缺陷")
    else:
        add("buffering 事件成对", "PASS" if abs(n_start - n_end) <= 1 else "FAIL",
            "START=%d END=%d" % (n_start, n_end),
            "允许差 1（收尾时最后一次可能在途中）")

    ovf = stats.get("steady", {}).get("dropOvf", {})
    if not ovf.get("n"):
        add("无 credit 记账失守", "INCONCLUSIVE", "dropOvf 无样本", "—")
    else:
        add("无 credit 记账失守", "PASS" if ovf.get("max") == 0 else "FAIL",
            "稳态 dropOvf max=%s" % ovf.get("max"),
            "vpark/apark 有增长即流控在工作")

    # 泄漏判据: 稳态段 RSS/fd 的斜率。**样本不足 120 秒不下结论** ——
    # 实测 5 分钟样本给出 +41.6 MB/小时, 而 269 秒内实际只涨 14MB 且落在
    # 区间波动内, 短样本的斜率外推不可靠
    def slope(seg, col):
        pts = [(int(r["t"]), r[col]) for r in seg
               if isinstance(r.get(col), float) and r[col] > -9000
               and isinstance(r.get("t"), float)]
        # 门槛 600 秒: 实证得来。5 分钟样本给出 RSS +41.6 MB/小时, 而 30 分钟
        # 样本给出 +3.6, **相差 11 倍** —— 短样本的斜率会被区间内的正常波动
        # 主导。原门槛 120 秒明显偏低。
        if len(pts) < 600:
            return None
        mx = sum(p[0] for p in pts) / len(pts)
        my = sum(p[1] for p in pts) / len(pts)
        d = sum((p[0] - mx) ** 2 for p in pts)
        return (sum((p[0] - mx) * (p[1] - my) for p in pts) / d) if d else 0.0

    steady_rows = segs.get("steady") or []
    for col, unit, tol in (("rssMb", "MB/小时", 20.0), ("fd", "个/小时", 60.0)):
        sl = slope(steady_rows, col)
        if sl is None:
            add("%s 无泄漏" % col, "INCONCLUSIVE",
                "稳态样本 %d 秒 < 600，斜率不可靠(实证: 5min 与 30min 相差 11 倍)" % len(steady_rows),
                "需更长样本")
        else:
            per_h = sl * 3600
            add("%s 无泄漏" % col,
                "PASS" if abs(per_h) < tol else "FAIL",
                "稳态斜率 %+.1f %s" % (per_h, unit),
                "区间 %.1f~%.1f" % (
                    min(r[col] for r in steady_rows if isinstance(r.get(col), float)),
                    max(r[col] for r in steady_rows if isinstance(r.get(col), float))))

    if not seek.get("count"):
        add("seek 追踪有记录", "INCONCLUSIVE",
            "seekTrace.count=%s" % seek.get("count"),
            "发起 %d 次" % n_seek)
    else:
        add("seek 追踪有记录", "PASS" if seek["count"] >= min(n_seek, 10) else "FAIL",
            "count=%d" % seek["count"], "发起 %d 次(环形上限 10)" % n_seek)

    # ---- seek 精度判据：与素材的真实帧栅格对照 ----
    #
    # 口径：精准 seek 的实现是"定位到目标前的关键帧 → 解码 → 丢弃到目标",
    # 所以**首帧应落在请求位置之后的第一个真实帧上**, 即
    #     accuracyMs ∈ [0, 帧间隔)          且
    #     请求位置 + accuracyMs 恰好落在帧栅格上
    #
    # 第二条才是有力的那条: 只看区间的话, 一个把请求值原样回传的实现
    # (accuracy 恒 0) 也能通过。落在帧栅格上则要求那个数与素材的真实
    # 帧率对得起来 —— 这是 VESeekTrace 里"回传的 pts 是不是请求值本身"
    # 那条怀疑的判别式。
    #
    # **不用 keyframes.txt 直接算期望落点**: 关键帧只决定解码从哪开始,
    # 决定首帧位置的是帧栅格。keyframes 在这里只用于确认"请求位置之前
    # 确实有关键帧"(即这次 seek 真的需要追帧), 作为场景有效性的旁证。
    kf = []
    kp = os.path.join(raw_dir, "keyframes.txt")
    if os.path.exists(kp):
        for line in io.open(kp, errors="ignore"):
            line = line.strip()
            if line:
                try:
                    kf.append(float(line))
                except ValueError:
                    pass

    items = [i for i in (seek.get("items") or [])
             if i.get("accuracyMs") is not None and not i.get("aborted")]
    fps_nom = fr
    if not fps_nom:
        add("seek 精度落在帧栅格上", "INCONCLUSIVE",
            "素材帧率未知，算不出帧间隔", "需 asset.json 的 r_frame_rate")
    elif not items:
        add("seek 精度落在帧栅格上", "INCONCLUSIVE",
            "无带精度的 seek 记录（%d 条里 0 条）" % len(seek.get("items") or []),
            "需至少 1 次带首帧的 seek")
    else:
        interval = 1000.0 / fps_nom
        # 容差取帧间隔的 5%: 时间戳在 mp4 里按 time_base 取整, 25fps/30fps
        # 都不是二进制整除, 逐帧累积的舍入可达零点几毫秒
        tol = interval * 0.05
        bad = []
        for i in items:
            a = i["accuracyMs"]
            landed = i["requestedMs"] + a
            off_grid = abs((landed / interval) - round(landed / interval)) * interval
            if not (-tol <= a < interval + tol) or off_grid > tol:
                bad.append((i["requestedMs"], a, off_grid))
        add("seek 精度落在帧栅格上",
            "PASS" if not bad else "FAIL",
            "%d/%d 条满足 accuracy∈[0,%.1fms) 且落在帧栅格上"
            % (len(items) - len(bad), len(items), interval)
            + ("" if not bad else "；越界: %s" % bad[:3]),
            "素材帧率 %.2f → 帧间隔 %.1fms；关键帧 %d 个"
            % (fps_nom, interval, len(kf)))

    # ---- seek 请求记账恒等式 ----
    #
    # requested = 执行 + merged + dropped。对不上就说明还有第四条"请求消失"
    # 的路径没被记下来 —— 而请求消失恰恰是最难发现的一类缺陷：它与
    # "seek 很快"在报告里长得一模一样。
    #
    # 交叉校验来源是**播放器之外的**：harness 自己发了多少次 seek，记在
    # env 的 seekPercents 里。播放器自报的 requested 必须与它相等 ——
    # 只用播放器内部三个数互相加减是自洽的废话，加上这一条才有意义。
    sr = st.get("seekRequested")
    if sr is None:
        add("seek 请求记账自洽", "INCONCLUSIVE",
            "快照无 seekRequested（native 未采集）", "需 VEPerfStats 的三个计数")
    elif n_seek == 0:
        add("seek 请求记账自洽", "INCONCLUSIVE",
            "本用例未发起 seek", "需带 seekPercents 的用例")
    else:
        merged = st.get("seekMerged") or 0
        dropped = st.get("seekDropped") or 0
        done = seek.get("count") or 0
        # 环形缓冲上限 10：执行超过 10 次时 count 会封顶，恒等式只在未封顶时
        # 可判 —— 封顶了就说不清差额是"丢了"还是"被环形挤掉了"
        if done >= 10 and sr > 10:
            add("seek 请求记账自洽", "INCONCLUSIVE",
                "执行 %d 次已达环形上限，差额无法归属" % done,
                "requested=%d merged=%d dropped=%d" % (sr, merged, dropped))
        else:
            internal = st.get("seekInternal") or 0
            ok_id = (sr + internal == done + merged + dropped)
            ok_x = (sr == n_seek)
            add("seek 请求记账自洽",
                "PASS" if (ok_id and ok_x) else "FAIL",
                "外部 %d + 内部 %d = 执行 %d + merged %d + dropped %d  %s"
                % (sr, internal, done, merged, dropped,
                   "恒等式成立" if ok_id else "**对不上，还有一条请求消失的路径没记**"),
                "harness 实发 %d 次 %s" % (
                    n_seek, "与外部请求数相符" if ok_x
                    else "**与播放器自报的外部请求数不符**"))

    # ---- seek 后队列峰值归零(perf-metrics 步骤2 遗留验收项) ----
    #
    # 采样时刻：seek 之后的独立统计窗口(post_seek 段)。峰值是"只涨不落"的
    # 累计量，seek 时若没被复位，post_seek 段的起点就会继承 seek 之前的高
    # 水位 —— 表现为"seek 后队列一直很深"，而实际是个没清的计数器。
    #
    # **判据不是"post_seek 首样本为 0"**：峰值每秒采一次，而 seek 后队列在
    # 一秒内就回填了，首样本本来就该 > 0。要求它为 0 会把正确实现判成 FAIL。
    #
    # 峰值是只涨不落的累计最大值，所以**序列出现下降就是复位发生过的证据**，
    # 而全程单调不降就是没复位。这是 1 秒粒度下唯一能证实的形式。
    peak_cols = [c for c in ("vqPeak", "aqPeak")
                 if any(isinstance(r.get(c), float) for r in rows)]
    if not seek_secs:
        add("seek 后队列峰值复位", "INCONCLUSIVE",
            "未发起 seek（或 seek 时刻不在日志窗口内）", "需带 seekPercents 的用例")
    elif not peak_cols:
        add("seek 后队列峰值复位", "INCONCLUSIVE",
            "时间线未采集队列峰值列（vqPeak/aqPeak）",
            "VESTAT 当前只有瞬时深度 vq/aq")
    else:
        detail, verdict = [], "FAIL"
        for c in peak_cols:
            ser = [(r.get("t"), r[c]) for r in rows if isinstance(r.get(c), float)]
            drops = [(t, prev, cur) for (_, prev), (t, cur)
                     in zip(ser, ser[1:]) if cur < prev]
            if drops:
                verdict = "PASS"
                detail.append("%s 下降 %d 次(如 t=%s %g→%g)"
                              % (c, len(drops), drops[0][0], drops[0][1], drops[0][2]))
            else:
                detail.append("%s 全程单调不降(%g→%g)"
                              % (c, ser[0][1], ser[-1][1]) if ser else "%s 无样本" % c)
        add("seek 后队列峰值复位", verdict, "；".join(detail),
            "seek 发生在第 %s 秒；峰值只涨不落，出现下降即证明复位" % seek_secs)

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
